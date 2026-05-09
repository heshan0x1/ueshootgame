// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MassProcessorDependencySolver.h
	-----------------------------------------------------------------------------
	【中文总览】Mass ECS 调度器的"大脑" —— 处理器依赖求解器
	-----------------------------------------------------------------------------

	这是整个 MassEntity 框架最核心、最复杂的算法层。
	它的使命：把"一堆带有读/写需求和 ExecuteBefore/After 声明的 Processor"
	→ 求解出一条"扁平的、无 race condition 的、最大化并行的执行序列"。

	== 为什么需要这个东西? ==
	传统 Tick 方案：人工指定 TickGroup、TickPrerequisite，写错了就崩 / 死锁 / 数据竞争。
	ECS 方案：每个 Processor 声明"我读 X、写 Y"，求解器自动推断出顺序：
	  - 两个 Processor 都"写"同一个 Fragment   → 必须串行
	  - 一个"写"另一个"读"同一个 Fragment      → 必须串行（写在前 / 读在前都视语境）
	  - 两个 Processor 都只"读"同一个 Fragment → 可以并行
	  - 两个 Processor 操作"不同 archetype"    → 即使都写同一类型也可以并行
	这样添加新 Processor 不用思考它该插在哪里、不会引入隐藏的 race。

	== 算法分阶段 ==
	  1. CreateNodes      ：把每个 Processor 转成 FNode；按 group 名嵌套构建组节点。
	  2. BuildDependencies：把 ExecuteBefore 翻转成对方的 ExecuteAfter；按名字解析成
	                       OriginalDependencies（数值索引边）；把 group 的依赖下发到
	                       它的子节点。
	  3. Solve           ：拓扑排序 + ResourceUsage 沿途插入"动态资源边"，依次输出
	                       一个无冲突的 NodeIndex 序列；同时记录 SequencePositionIndex
	                       （供并行 dispatch 使用）。

	== 关键概念速查 ==
	  - 静态边（user-declared）：Processor.ExecuteBefore / ExecuteAfter
	  - 动态边（自动推出）     ：FResourceUsage 在 SubmitNode 时根据当前 readers/writers
	                             生成 OriginalDependencies
	  - transient 边           ：Solve 过程的"剩余依赖"副本，依次扣除直到为 0 才能出序
	  - SequencePositionIndex  ：节点在执行序列中的"层数"（最长依赖链长度），决定了
	                             ParallelDispatch 需要多少个 fence/event
	  - MaxExecutionPriority   ：高优先级节点会"传染"给所有依赖它的祖先节点，让排序
	                             时高优先级链整体往前

	== 调度器 vs Tick 的区别（写给后人看）==
	UE 传统 Tick 是"被动声明 + 引擎按 group 拉动"；
	Mass 的 DependencySolver 是"主动求解 + 一次构建多次复用"，每次 archetype 集合
	变化（FResult.ArchetypeDataVersion 不同）才会重新求解，运行时近乎零成本。
=============================================================================*/

#pragma once

#include "MassProcessingTypes.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "Containers/StaticArray.h"

#define UE_API MASSENTITY_API


class UMassProcessor;
namespace UE::Mass
{
	struct FTypeManager;
}

/**
 * 【中文】描述对资源（Fragment、Subsystem...）的访问操作类型。
 * 之所以用 namespace + constexpr 而不是 enum class，是为了能直接当索引用
 * （例如 TMassExecutionAccess<T>::operator[](OpIndex)）。
 *   - Read = 0：只读访问，多个 Processor 可同时读同一资源
 *   - Write = 1：读写访问，与其它任何 Read/Write 都互斥（除非 archetype 不重叠）
 *   - MAX = 2：用作循环上界 / 数组大小
 */
namespace EMassAccessOperation
{
	constexpr uint32 Read = 0;
	constexpr uint32 Write = 1;
	constexpr uint32 MAX = 2;
};

/**
 * 【中文】"读+写"成对存放的小容器。
 * 把任意类型 T 复制两份，分别表示 Read 集合和 Write 集合。
 * 主要用法：T = FMassFragmentBitSet 时，Read/Write 各保存一个 fragment 位集，
 *           分别表示"我要读哪些 fragment"和"我要写哪些 fragment"。
 *
 * @tparam T 通常是某种 BitSet（FMassFragmentBitSet / FMassChunkFragmentBitSet ...）
 *           或是 FResourceAccess 等求解器内部类型。
 */
template<typename T>
struct TMassExecutionAccess
{
	/** 读访问集合。多个 Processor 可同时读 → 不会冲突。 */
	T Read;
	/** 写访问集合。任何与之相交的 Read/Write 都需要排队。 */
	T Write;

	/**
	 * 用 EMassAccessOperation 索引取出对应的容器引用。
	 * @param OpIndex 必须 ≤ EMassAccessOperation::MAX
	 * @return 0 → Read；1 → Write
	 */
	T& operator[](const uint32 OpIndex)
	{
		check(OpIndex <= EMassAccessOperation::MAX);
		return OpIndex == EMassAccessOperation::Read ? Read : Write;
	}

	const T& operator[](const uint32 OpIndex) const
	{
		check(OpIndex <= EMassAccessOperation::MAX);
		return OpIndex == EMassAccessOperation::Read ? Read : Write;
	}

	/** 把 Read、Write 当成一个 2 元素数组对外暴露，便于循环遍历两种访问模式。 */
	TConstArrayView<T> AsArrayView() const { return MakeArrayView(&Read, 2); }

	/** Read 与 Write 都为空才视为整体为空。 */
	bool IsEmpty() const { return Read.IsEmpty() && Write.IsEmpty(); }
};

/** 
 * TMassExecutionAccess specialization for FMassConstSharedFragmentBitSet to enforce lack of access (not needed) and
 * no "Write" component (conceptually doesn't make sense).
 *
 * 【中文】针对"只读共享 Fragment"的特化。
 * ConstSharedFragment 在概念上就是只读不可写的（多个 Processor 共享、不允许任意一个改），
 * 因此这里删除了 Write 字段——既节省内存，也让"不小心写它"在编译期就报错。
 * AsArrayView() 也只暴露 1 个元素。
 */
template<>
struct TMassExecutionAccess<FMassConstSharedFragmentBitSet>
{
	/** 只有 Read，没有 Write —— 如果你需要写，请用普通 SharedFragment。 */
	FMassConstSharedFragmentBitSet Read;
	TConstArrayView<FMassConstSharedFragmentBitSet> AsArrayView() const 
	{ 
		return MakeArrayView(&Read, 1); 
	}
	bool IsEmpty() const 
	{ 
		return Read.IsEmpty(); 
	}
};

/**
 * 【中文】单个 Processor 的"读写指纹"。
 * 求解器用它来判断两个 Processor 是否冲突 → 是否需要插入依赖边。
 * 字段语义：
 *   Read[X]  = 我需要读  X
 *   Write[X] = 我需要写  X
 *
 * 包含 5 类资源：
 *   - Fragments            ：Entity 级别的 component 数据，最常见
 *   - ChunkFragments       ：Chunk 级共享数据（per-chunk）
 *   - SharedFragments      ：Archetype 级共享数据（per-archetype，可写）
 *   - ConstSharedFragments ：Archetype 级共享只读数据（不计入冲突，无需追踪）
 *   - RequiredSubsystems   ：依赖的外部 UWorldSubsystem 等
 *
 * 还包含 tag 过滤：
 *   - RequiredAllTags  : 必须同时拥有这些 tag
 *   - RequiredAnyTags  : 至少有其中一个 tag
 *   - RequiredNoneTags : 不允许拥有这些 tag
 * 这部分主要用于把 Requirements 转换回 FMassArchetypeCompositionDescriptor 时的 tag 集。
 */
struct FMassExecutionRequirements
{
	/**
	 * 把另一个 Requirements 合并进来（位集做并集，tag 也做并集）。
	 * 用途：group 节点把所有子节点的 Requirements 累计起来；FResourceUsage 跟踪
	 *       "目前已 submit 节点的总需求"。
	 * 注意：会把 ResourcesUsedCount 重置为 INDEX_NONE（强制下次重新计算）。
	 */
	UE_API void Append(const FMassExecutionRequirements& Other);

	/** 重新统计 Read+Write 中所有"位"的总数，结果写入 ResourcesUsedCount。 */
	UE_API void CountResourcesUsed();

	/** 总位数（资源位 + tag 位），用于 hash / 调试统计。会顺便调用 CountResourcesUsed()。 */
	UE_API int32 GetTotalBitsUsedCount();

	/** 5 类资源 + 3 类 tag 全为空才算空。 */
	UE_API bool IsEmpty() const;

	/**
	 * 把 Requirements 转换成"虚拟 archetype"的组成描述符。
	 * 用于 ResolveDependencies 内部："为每个 Processor 创建一个匹配它需求的 archetype"，
	 * 然后让所有 Processor 用 query 去匹配，找出 archetype 重叠关系（archetype-aware
	 * 冲突检测的基础）。
	 *
	 * 注意：Read 和 Write 在这里被合并 —— archetype 描述符本身不区分访问模式。
	 */
	UE_API FMassArchetypeCompositionDescriptor AsCompositionDescriptor() const;

	/** Entity 级 component 数据。绝大多数 Processor 主要操作这里。 */
	TMassExecutionAccess<FMassFragmentBitSet> Fragments;
	/** Chunk 级共享数据（同一个 chunk 内多个 entity 共用一份）。 */
	TMassExecutionAccess<FMassChunkFragmentBitSet> ChunkFragments;
	/** Archetype 级共享数据（可写）。 */
	TMassExecutionAccess<FMassSharedFragmentBitSet> SharedFragments;
	/** Archetype 级共享只读数据 —— 概念上永不冲突，求解器在 SubmitNode 中也不追踪它。 */
	TMassExecutionAccess<FMassConstSharedFragmentBitSet> ConstSharedFragments;
	/** 外部 Subsystem 引用。GameThreadOnly 的 subsystem 会强制 GT 调度；bThreadSafeWrite 的会被剥离掉。 */
	TMassExecutionAccess<FMassExternalSubsystemBitSet> RequiredSubsystems;
	/** 该 Processor 要求 entity 同时具备的所有 tag。 */
	FMassTagBitSet RequiredAllTags;
	/** 该 Processor 要求 entity 至少具备其一的 tag。 */
	FMassTagBitSet RequiredAnyTags;
	/** 该 Processor 要求 entity 不能拥有的 tag。 */
	FMassTagBitSet RequiredNoneTags;
	/** 缓存的资源位总数，INDEX_NONE 表示需要重新统计。 */
	int32 ResourcesUsedCount = INDEX_NONE;
};

/**
 * 【中文】Mass 框架的"调度求解器"。
 * 它是一次性的对象：构造时拿到一组 Processor，调用 ResolveDependencies 后输出
 * 一个排好序的 FMassProcessorOrderInfo 列表，然后通常就被丢弃。
 *
 * 求解流程（详见 ResolveDependencies）：
 *   1) 创建 root 节点（dummy "顶层组"）
 *   2) 为每个 Processor 创建 FNode（CreateNodes），按 group 名构建组层级
 *   3) 为每个 Processor 创建一个虚拟 archetype，让所有 Processor 互相用 query 匹配
 *      → 为每个 Node 算出 ValidArchetypes（archetype-aware 冲突剪枝的基础）
 *   4) Pruning：query 没匹配到任何 archetype 的 Processor → 标记为 group（不再执行）
 *   5) BuildDependencies：把 ExecuteBefore 翻转成对方 ExecuteAfter；按名字解析成
 *      OriginalDependencies；把 group 依赖向下推到子节点
 *   6) Solve：循环挑选"无剩余依赖且不与已 submit 节点资源冲突"的节点输出，过程中
 *      用 FResourceUsage 维护"目前哪些节点正读/写哪些资源"，并据此向新节点动态
 *      插入更多依赖边
 *   7) 计算 SequencePositionIndex / MaxSequenceLength（用于 ParallelDispatch 的事件数）
 */
struct FMassProcessorDependencySolver
{
	/**
	 * 【中文】求解器内部的"节点"。一个节点要么代表一个 Processor，要么代表一个 group。
	 * 注意它不是 Processor 的简单 wrapper —— 它额外承载了"求解过程中的状态"
	 * （TransientDependencies / TotalWaitingNodes / SequencePositionIndex ...）。
	 *
	 * 节点之间的关系全部通过 NodeIndex（在 AllNodes 数组中的下标）表达，便于排序时
	 * 直接对索引数组做操作而无需移动节点本身。
	 */
	struct FNode
	{
		FNode(const FName InName, UMassProcessor* InProcessor, const int32 InNodeIndex = INDEX_NONE) 
			: Name(InName), Processor(InProcessor), NodeIndex(InNodeIndex)
		{}

		/** Processor == nullptr 表示这是个 group 节点（或被 prune 过的 processor）。 */
		bool IsGroup() const { return Processor == nullptr; }
		/**
		 * @return `true` when everything's fine, `false` when cycles have been encountered. If that
		 *	happens `OutCycleIndices` gets filled with the relevant node indices.
		 *
		 * 【中文】递归地把"有多少节点在等我"+1。
		 * 用途：求解前先做一遍 DFS，统计每个节点在依赖链中的"被等待次数"。被等待越多
		 *      = 越靠近根 = 应该越早执行。然后按 TotalWaitingNodes 降序排，让"被很多
		 *      人等的节点"先尝试出列。
		 *
		 * @param InAllNodes      所有节点的可写视图（递归调用依赖于此）
		 * @param IterationsLimit 防止无限递归的预算 —— 通常等于"有依赖的节点数"。每深入
		 *                        一层就 -1，<0 视为发现了环
		 * @param OutCycleIndices 发现环时把途径节点 index 收集到这里（自底向上），上层
		 *                        据此报错并断掉一条边
		 * @return false 表示发现了环
		 */
		bool IncreaseWaitingNodesCount(TArrayView<FNode> InAllNodes, const int32 IterationsLimit, TArray<int32>& OutCycleIndices);

		/**
		 * 【中文】IncreaseWaitingNodesCount 的"带优先级传染"版本。
		 * 每次往上递归时，把当前节点的 MaxExecutionPriority 当作 ChildPriority 传给
		 * 父节点，父节点用 UpdateExecutionPriority 取较大值并 +1（保证"被依赖者优先级
		 * ≥ 依赖者优先级 + 1"）。
		 * 效果：高优先级节点会拉高所有它依赖的祖先节点的优先级，求解时这些祖先就会
		 *      被排在最前面 → 把整条高优先级链整体向前推。
		 */
		bool IncreaseWaitingNodesCountAndPriority(TArrayView<FNode> InAllNodes, const int32 IterationsLimit, TArray<int32>& OutCycleIndices
			, const int32 InChildPriority = TNumericLimits<int32>::Min());

		/**
		 * 用子节点优先级更新自己的 MaxExecutionPriority。
		 * 公式：MaxExecutionPriority = max(ChildPriority + 1, MaxExecutionPriority)
		 * 之所以 +1，是为了保证"父节点（被依赖者）严格高于子节点"，整条链能稳定排序。
		 */
		void UpdateExecutionPriority(const int32 ChildExecutionPriority)
		{
			// picking the max execution priority - note that we're increasing child
			// priority to ensure dependencies  always have a higher stored priority
			// than the nodes that depend on them
			MaxExecutionPriority = FMath::Max(int32(ChildExecutionPriority) + 1, MaxExecutionPriority);
		}


		/** Processor 的查找名（Class FName 或 Instance FName）；group 则是 group 路径名。 */
		FName Name = TEXT("");
		/** 真正的 Processor 指针；nullptr 表示 group 节点或 prune 后的节点。 */
		UMassProcessor* Processor = nullptr;
		/**
		 * 该节点的依赖列表（指向其它 NodeIndex）。
		 * 包含两种来源：
		 *   1) 用户声明的 ExecuteAfter / 反转过来的 ExecuteBefore
		 *   2) FResourceUsage::SubmitNode 时根据资源冲突动态插入的边
		 * 求解结束后会做为 FMassProcessorOrderInfo 的依赖列表对外暴露。
		 */
		TArray<int32> OriginalDependencies;
		/**
		 * Solve 过程中的"还剩多少依赖没满足"的副本。
		 * 每当一个节点被 submit，就从所有其它节点的 TransientDependencies 中移除它；
		 * 当某节点的 TransientDependencies 空了，它就可以被 submit。
		 */
		TArray<int32> TransientDependencies;
		/** 用户声明"我必须在 X 之前执行"的列表（按 FName）。BuildDependencies 中会被翻转给 X。 */
		TArray<FName> ExecuteBefore;
		/** 用户声明"我必须在 X 之后执行"的列表（按 FName）。最终被解析为 OriginalDependencies。 */
		TArray<FName> ExecuteAfter;
		/** 该 Processor 的读写指纹（group 节点这里通常是空的，依赖都在子节点上）。 */
		FMassExecutionRequirements Requirements;
		/** 在 AllNodes 中的下标 —— 整个求解器的"节点身份证"。 */
		int32 NodeIndex = INDEX_NONE;
		/** indicates how often given node can be found in dependencies sequence for other nodes
		 *
		 *  【中文】"有多少节点直接或间接依赖我"。被依赖越多 = 越关键 = 越早出列。
		 *  在 IncreaseWaitingNodesCount 的 DFS 中累加。 */
		int32 TotalWaitingNodes = 0;
		/**
		 * Indicates the maximum execution priority represented by this node or any od the nodes
		 * that depend on it in a logical sense - i.e. it does not include the nodes that are dependencies just by blocing required resources
		 * @todo reword this comment
		 * Note that we're using a larger type than UMassProcessor.ExecutionPriority (int32 vs int16)
		 * to not have to handle overflow in UpdateExecutionPriority
		 *
		 * 【中文】"经过传染后"的最大执行优先级。
		 * 初始值 = Processor.GetExecutionPriority()；通过 IncreaseWaitingNodesCountAndPriority
		 * 沿依赖图向上传播取 max(child+1, self) 的结果。
		 * 用类型 int32 而非 int16 是为了避免链很长时 +1 累加溢出。
		 */
		int32 MaxExecutionPriority = 0;
		/** 
		 * indicates how deep within dependencies graph this give node is, or in other words, what's the longest sequence 
		 * from this node to a dependency-less "parent" node 
		 *
		 * 【中文】节点所处的"层数" —— 从无依赖的根节点到本节点的最长链长度。
		 * 在 PerformSolverStep 中递推：当前节点的 SequencePositionIndex
		 *   = max(所有依赖节点的 SequencePositionIndex) + 1
		 * 决定了 ParallelDispatch 时这个节点要等到第几个 fence 才能开始。
		 * 整个求解结束后，全场最大值就是 MaxSequenceLength —— 即并行执行需要多少
		 * 个事件/同步点。
		 */
		int32 SequencePositionIndex = 0;
		/** 该 group 节点直接包含的子节点 NodeIndex（仅 group 节点使用）。 */
		TArray<int32> SubNodeIndices;
		/** 
		 * 该 Processor 的 query 能匹配到的所有 archetype。
		 * 用途：archetype-aware 冲突剪枝 —— 即使 A 写 X、B 读 X，但如果它们操作的
		 * archetype 集合不重叠，仍可并行；判断这一点正是靠 ValidArchetypes。
		 * 在 ResolveDependencies 中通过虚拟 archetype + Processor.GetArchetypesMatchingOwnedQueries 填充。
		 */
		TArray<FMassArchetypeHandle> ValidArchetypes;
	};

private:
	/**
	 * 【中文】"目前每个资源谁在读 / 谁在写"的运行时账本。
	 * 求解器一边按拓扑顺序往输出序列里 push 节点（SubmitNode），一边在这本账上
	 * 登记"现在 NodeX 正在读 FragmentY"。当下一个候选节点尝试出列时：
	 *   - 它要读 Y → 必须排在所有目前写 Y 的节点之后（动态边）
	 *   - 它要写 Y → 必须排在所有目前读 Y 和写 Y 的节点之后（动态边）
	 * 这就是"自动避免数据竞争"的核心机制。
	 */
	struct FResourceUsage
	{
		FResourceUsage(const TArray<FNode>& InAllNodes);

		/**
		 * 检查 TestedRequirements 是否能"无冲突"地立即执行（不需要插入新动态边）。
		 * 一旦判出冲突，会再用 archetype 做一次精细化判断 —— 操作 archetype 不重叠
		 * 的可以放行（HasArchetypeConflict）。
		 * @param TestedRequirements 候选节点的读写指纹
		 * @param InArchetypes       候选节点匹配到的 archetype（用于精细化剪枝）
		 * @return true 表示该候选可"干净地"插入（不会与已 submit 节点抢资源）
		 */
		bool CanAccessRequirements(const FMassExecutionRequirements& TestedRequirements, const TArray<FMassArchetypeHandle>& InArchetypes) const;

		/**
		 * 把节点登记到账本上：根据它的 Read/Write 集合，向它的 OriginalDependencies
		 * 里追加"必须等待之前 reader/writer 完成"的边，然后把自己标记为新的 reader/writer。
		 * 注意：这是会改 InOutNode 的 —— 它会动态加边！副作用很大。
		 */
		void SubmitNode(const int32 NodeIndex, FNode& InOutNode);

	private:
		/** 一个资源（如 fragment 类型 N）当前"哪些节点在使用我"的列表。 */
		struct FResourceUsers
		{
			/** 节点 NodeIndex 列表。同一类型可同时有多个 Reader 或 Writer。 */
			TArray<int32> Users;
		};
		
		/** 某种"资源类别"（如所有 Fragment 类型）的整张访问表 —— 每个类型一条记录。 */
		struct FResourceAccess
		{
			/** Access[i] = 资源类型 i 当前的 Reader/Writer 列表。 */
			TArray<FResourceUsers> Access;
		};
		
		/** 已 submit 节点的总读写需求（用于快速短路 CanAccess 判断）。 */
		FMassExecutionRequirements Requirements;
		/** Fragment 维度的 Reader/Writer 表（[Read]/[Write] 两套）。 */
		TMassExecutionAccess<FResourceAccess> FragmentsAccess;
		/** ChunkFragment 维度的 Reader/Writer 表。 */
		TMassExecutionAccess<FResourceAccess> ChunkFragmentsAccess;
		/** SharedFragment 维度的 Reader/Writer 表。 */
		TMassExecutionAccess<FResourceAccess> SharedFragmentsAccess;
		/** Subsystem 维度的 Reader/Writer 表（archetype 不参与判断 —— Subsystem 是全局的）。 */
		TMassExecutionAccess<FResourceAccess> RequiredSubsystemsAccess;
		/** 对 AllNodes 的只读视图 —— 我们要查每个 user 节点的 ValidArchetypes。 */
		TConstArrayView<FNode> AllNodesView;

		/**
		 * 处理某一类资源（Fragment / Subsystem ...）的访问登记。
		 * 详细算法见 .cpp 中的 3 步实现注释（Read 等 Writer / Write 等所有 / 登记自己）。
		 * @tparam TBitSet 资源类型的位集（FMassFragmentBitSet 等）
		 * @param ElementAccess       要更新的资源账本（FragmentsAccess 等）
		 * @param TestedRequirements  当前节点对该类资源的 Read/Write 需求
		 * @param InOutNode           要登记的节点（会被加新的动态依赖边）
		 * @param NodeIndex           InOutNode 在 AllNodes 中的下标
		 */
		template<typename TBitSet>
		void HandleElementType(TMassExecutionAccess<FResourceAccess>& ElementAccess
			, const TMassExecutionAccess<TBitSet>& TestedRequirements, FMassProcessorDependencySolver::FNode& InOutNode, const int32 NodeIndex);

		/**
		 * 纯位集层面的"是否可访问"快速判断。
		 * 测试 Write∩StoredWrite、Write∩StoredRead、Read∩StoredWrite 三个交集是否为空。
		 * 任何一个非空 = 有冲突。
		 */
		template<typename TBitSet>
		static bool CanAccess(const TMassExecutionAccess<TBitSet>& StoredElements, const TMassExecutionAccess<TBitSet>& TestedElements);

		/** Determines whether any of the Elements' (i.e. Fragment, Tag,...) users operate on any of the archetypes given via InArchetypes
		 *
		 * 【中文】当位集层面已经判出冲突后，再用 archetype 集合精细化判断：
		 *   - 如果当前所有 user 的 ValidArchetypes 都不与 InArchetypes 重叠 → 实际上不冲突，放行
		 *   - 否则 → 真冲突，需要插入依赖边
		 * 这是 archetype-aware 调度优化的关键。 */
		bool HasArchetypeConflict(TMassExecutionAccess<FResourceAccess> ElementAccess, const TArray<FMassArchetypeHandle>& InArchetypes) const;
	};

public:
	/** Optionally returned by ResolveDependencies and contains information about processors that have been pruned and 
	 *  other potentially useful bits. To be used in a transient fashion.
	 *
	 *  【中文】ResolveDependencies 的"附加输出"。
	 *  调用方传入 InOutOptionalResult 后可以拿到：
	 *    - DependencyGraphFileName：生成 GraphViz dot 文件的路径（调试可视化）
	 *    - PrunedProcessors      ：被剪掉的 Processor 列表（query 一个 archetype 都没匹配到）
	 *    - MaxSequenceLength     ：最长依赖链 + 1，决定 ParallelDispatch 用多少 event
	 *    - ArchetypeDataVersion  ：求解时刻的 archetype 数据版本，用于缓存失效判断
	 *  IsResultUpToDate 拿这个 FResult 比对当前 EntityManager 决定是否需要重新求解。 */
	struct FResult
	{
		/** GraphViz dot 文件输出路径（可视化求解结果，留空则不输出）。 */
		FString DependencyGraphFileName;
		/** 被 query-based pruning 剪掉的 Processor —— 它们在当前 archetype 集合下没数据可处理。 */
		TArray<TObjectPtr<UMassProcessor>> PrunedProcessors;
		/** 整个求解后最长的依赖链层数，等价于 max(SequencePositionIndex)。 */
		int32 MaxSequenceLength = 0;
		/** 求解时 EntityManager 的 archetype 版本号 —— 缓存失效用。 */
		uint32 ArchetypeDataVersion = 0;
		UE_DEPRECATED(5.6, "This property is deprecated, replaced by PrunedProcessors")
		TArray<TSubclassOf<UMassProcessor>> PrunedProcessorClasses;

		void Reset()
		{
			PrunedProcessors.Reset();
			MaxSequenceLength = 0;
			ArchetypeDataVersion = 0;
		}
	};

	/**
	 * 构造求解器。
	 * @param InProcessors    要参与排序的 Processor 集合（注意是数组视图，调用者保证生命周期）
	 * @param bIsGameRuntime  true = 真实运行时（影响 ShouldAllowQueryBasedPruning）；
	 *                        false = 编辑器/调试，不剪枝。
	 */
	MASSENTITY_API FMassProcessorDependencySolver(TArrayView<UMassProcessor* const> InProcessors, const bool bIsGameRuntime = true);

	/**
	 * 主入口：求解依赖并把扁平执行序列写入 OutResult。
	 * @param OutResult            输出的执行序列（每项是 FMassProcessorOrderInfo）
	 * @param EntityManager        可选；如果给了就用它的 archetype 信息做剪枝；
	 *                             不给则内部 new 一个临时 EntityManager 创建虚拟 archetype
	 * @param InOutOptionalResult  可选；用于回传剪枝列表 / 最长链 / archetype 版本 / 调试图文件路径
	 * 算法步骤详见 .cpp 内注释。
	 */
	MASSENTITY_API void ResolveDependencies(TArray<FMassProcessorOrderInfo>& OutResult, TSharedPtr<FMassEntityManager> EntityManager = nullptr, FResult* InOutOptionalResult = nullptr);

	/**
	 * 把"A.B.C"形式的 group 名拆成层级数组 ["A", "A.B", "A.B.C"]。
	 * 方便 CreateNodes 自下而上为每一级 group 创建 / 查找节点。
	 */
	MASSENTITY_API static void CreateSubGroupNames(FName InGroupName, TArray<FString>& SubGroupNames);

	/** Determines whether the dependency solving that produced InResult will produce different results if run with a given EntityManager
	 *
	 *  【中文】缓存有效性检查 —— "上次求解的 InResult 现在还能用吗？"
	 *  策略：
	 *    1) 如果之前没有任何 prune  → 永远还能用
	 *    2) 如果 archetype 数据版本没变 → 还能用
	 *    3) 否则逐个检查 PrunedProcessors，是否有谁现在又能匹配到 archetype 了
	 *       → 任一可以匹配 = 必须重新求解 */
	static bool IsResultUpToDate(const FResult& InResult, TSharedPtr<FMassEntityManager> EntityManager);

	/** 当前是否在为单线程平台求解（决定要不要做精细的资源/archetype 冲突检查）。 */
	bool IsSolvingForSingleThread() const { return bSingleThreadTarget; }

protected:
	// note that internals are protected rather than private to support unit testing
	// 【中文】这些原本应是 private 的，开成 protected 是为了让单测继承本类直接访问内部状态。

	/**
	 * Traverses InOutIndicesRemaining in search of the first RootNode's node that has no dependencies left. Once found 
	 * the node's index gets added to OutNodeIndices, removed from dependency lists from all other nodes and the function 
	 * quits.
	 * @return 'true' if a dependency-less node has been found and added to OutNodeIndices; 'false' otherwise.
	 *
	 * 【中文】Solve 主循环的"一步"：扫一遍 InOutIndicesRemaining，挑出一个可以现在就执行的节点。
	 * 挑选优先级（从高到低）：
	 *   A. TransientDependencies 已空 + 不与已 submit 节点资源冲突 → AcceptedNodeIndex（最优）
	 *   B. TransientDependencies 已空但有资源冲突                    → FallbackAcceptedNodeIndex
	 * 如果开启 PickHigherPriorityNodesRegardlessOfRequirements，则一旦剩余节点的优先级
	 * 已经低于队首优先级，就不再"贪心找完美匹配"，直接用 fallback。
	 *
	 * 出列后做 4 件事：
	 *   1) ResourceUsage.SubmitNode（更新账本 + 给节点加动态边）
	 *   2) 从 InOutIndicesRemaining 移除
	 *   3) 计算 SequencePositionIndex = max(deps.Seq) + 1
	 *   4) 从所有剩余节点的 TransientDependencies 中移除自己（解锁后继）
	 *
	 * @return false 表示没有任何节点可挑（说明剩下的全是环 → Solve 主循环会断一条边再来）
	 */
	bool PerformSolverStep(FResourceUsage& ResourceUsage, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices);
	
	/**
	 * 为单个 Processor 创建节点，并按 group 路径自动创建/复用所有祖先 group 节点。
	 * @return 新创建（或已存在）的 Processor 节点的 NodeIndex
	 */
	int32 CreateNodes(UMassProcessor& Processor);

	/**
	 * 把节点上的 ExecuteBefore 翻转给目标节点的 ExecuteAfter，再把 ExecuteAfter 解析
	 * 成 OriginalDependencies（按 NodeIndex），最后把 group 的依赖向下传给所有子节点。
	 * 缺失的依赖名会被建一个 dummy group 节点（保证间接传递依赖仍然成立）。
	 */
	void BuildDependencies();

	/**
	 * 拓扑排序 + 资源冲突感知的求解主循环。
	 * 调用 PerformSolverStep 直到 IndicesRemaining 空；遇到环就强制断一条边再继续。
	 * 最终把节点按求解顺序写入 OutResult（FMassProcessorOrderInfo 列表）。
	 */
	void Solve(TArray<FMassProcessorOrderInfo>& OutResult);

	/** 调试用：递归打印整个节点树（含 group 嵌套与 ExecuteBefore/After）。 */
	void LogNode(const FNode& Node, int Indent = 0);

	/** Finds out which subsystems handle multithreaded RW operations, and caches the result in MultiThreadedSystemsBitSet
	 *  【中文】扫一遍 TypeManager 的所有已注册 Subsystem，把那些标记为 bThreadSafeWrite 的
	 *  存到 MultiThreadedSystemsBitSet 里 —— 这些 subsystem 即使被多个 Processor 同时
	 *  写也不会有竞争，可以从依赖检测中"剥离"出去（CreateNodes 中减掉）。 */
	void GatherSubsystemInformation(const UE::Mass::FTypeManager& TypeManager);
	
	/** 输入：要参与排序的 Processor 集合（视图，本类不持有所有权）。 */
	TArrayView<UMassProcessor* const> Processors;

	/**
	 * indicates whether we're generating processor order to be run in single-threaded or multithreaded environment (usually
	 * this means Dedicated Server vs Any other configuration). In Single-Threaded mode we can skip a bunch of expensive,
	 * fine-tuning tests.
	 * @Note currently the value depends on MASS_DO_PARALLEL and there's no way to configure it otherwise, but there's 
	 * nothing inherently stopping us from letting users configure it.
	 *
	 * 【中文】是否在为单线程目标求解。
	 * 单线程模式下 PerformSolverStep 会跳过 CanAccessRequirements 这种昂贵的资源 +
	 * archetype 重叠检查 —— 反正都串行执行，没必要避免冲突。
	 * 当前由 MASS_DO_PARALLEL 编译宏决定（典型场景：DS = MASS_DO_PARALLEL=0）。
	 */
	const bool bSingleThreadTarget = bool(!MASS_DO_PARALLEL);
	/** 是否真实运行时（决定 ShouldAllowQueryBasedPruning 的参数）。 */
	const bool bGameRuntime = true;
	/** GraphViz dot 输出路径（调试可视化），从 InOutOptionalResult 复制过来。 */
	FString DependencyGraphFileName;
	/** 求解器内部所有节点（root group + group 节点 + processor 节点 + dummy 节点）。 */
	TArray<FNode> AllNodes;
	/** "节点名 → AllNodes 索引"映射，CreateNodes/BuildDependencies 中按名字查找用。 */
	TMap<FName, int32> NodeIndexMap;

	/** Stores the subsystems we know of that handle multithreaded access well - we filter those out, we don't need to consider them.
	 *  【中文】线程安全可并写的 Subsystem 位集 —— 它们对求解器"不存在"。
	 *  CreateNodes 中会从每个 Processor 的 RequiredSubsystems.Read/Write 中减掉这些位。 */
	FMassExternalSubsystemBitSet MultiThreadedSystemsBitSet;
};

#undef UE_API
