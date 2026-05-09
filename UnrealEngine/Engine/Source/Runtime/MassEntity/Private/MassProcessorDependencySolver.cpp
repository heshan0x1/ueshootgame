// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MassProcessorDependencySolver.cpp
	-----------------------------------------------------------------------------
	【中文总览】Mass ECS 处理器依赖求解器实现 —— 整个框架的并行调度大脑

	本文件是 ECS 调度算法的核心实现，把 Processor 集合 + 读写需求 + 用户声明的
	ExecuteBefore/After + 组结构 求解出一条无 race condition 的扁平执行序列。

	算法主流程（ResolveDependencies）：
	  1) 创建 root + 各 group + processor 的 FNode（CreateNodes）
	  2) 为每个 Processor 创建虚拟 archetype 让 query 互相匹配 → 算 ValidArchetypes
	  3) Pruning：对没匹配到任何 archetype 的 Processor 标 nullptr
	  4) BuildDependencies：ExecuteBefore→Other.ExecuteAfter→OriginalDependencies
	  5) Solve：循环 PerformSolverStep
	     - 每步在 IndicesRemaining 中找一个"TransientDependencies 已空 + 资源不冲突"
	       的节点，把它"运行"掉（追加到 SortedNodeIndices），并 SubmitNode 给账本
	     - 没有完美匹配就退而求其次（fallback）；遇到环就强行断一条边
	  6) 计算 SequencePositionIndex，输出 OutResult

	可视化：DependencyGraphFileName 不为空时会输出 GraphViz dot 文件。
	缓存：FResult.ArchetypeDataVersion 与 EntityManager 当前版本一致即可复用。
=============================================================================*/

#include "MassProcessorDependencySolver.h"
#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "MassTypeManager.h"
#include "Logging/MessageLog.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#if WITH_MASSENTITY_DEBUG
#include "Algo/Count.h"
#endif // WITH_MASSENTITY_DEBUG

#define LOCTEXT_NAMESPACE "Mass"

DEFINE_LOG_CATEGORY_STATIC(LogMassDependencies, Warning, All);

namespace UE::Mass::Private
{
	/** 把一个 FName 视图格式化成 "[A, B, C]" 形式的字符串，仅供调试日志使用。 */
	FString NameViewToString(TConstArrayView<FName> View)
	{
		if (View.Num() == 0)
		{
			return TEXT("[]");
		}
		FString ReturnVal = FString::Printf(TEXT("[%s"), *View[0].ToString());
		for (int i = 1; i < View.Num(); ++i)
		{
			ReturnVal += FString::Printf(TEXT(", %s"), *View[i].ToString());
		}
		return ReturnVal + TEXT("]");
	}

	/**
	 * 检查两个 archetype 容器是否有交集。
	 * 用途：archetype-aware 冲突剪枝 —— 即使两个 Processor 同写一个 fragment 类型，
	 * 但只要它们操作的 archetype 完全不重叠，就不会有数据竞争。
	 */
	bool DoArchetypeContainersOverlap(TConstArrayView<FMassArchetypeHandle> A, const TArray<FMassArchetypeHandle>& B)
	{
		for (const FMassArchetypeHandle& HandleA : A)
		{
			if (B.Contains(HandleA))
			{
				return true;
			}
		}
		return false;
	}

#if WITH_MASSENTITY_DEBUG
	/**
	 * 【中文】把检测到的依赖环按可读形式打印出来 —— 仅 debug 构建有这个能力。
	 *
	 * 步骤：
	 *   1) 在 CycleIndices 里找"出现两次以上"的节点 → 这就是环的起点
	 *   2) 找到环的长度（再次出现起点的位置 - 起点位置）
	 *   3) 计算环的稳定 hash（从环里最小 NodeIndex 的元素出发滚动 HashCombine），
	 *      这样无论从环上哪个节点开始检测都能得到相同 hash → 同一个环只报一次
	 *   4) 如果是新环，逐节点打印 ExecuteBefore / ExecuteAfter / Group 信息
	 *
	 * 这是排查"循环依赖"问题最重要的诊断输出。
	 */
	void LogCycle(TArray<FMassProcessorDependencySolver::FNode> AllNodes, TConstArrayView<int32> CycleIndices, TArray<uint32>& InOutReportedCycleHashes)
	{
		check(CycleIndices.Num());
		// We extract unique indices involved in the cycle below.
		// Note that we want to preserve the order since it will provide more meaningful debugging context.
		// But we do find the "lowest node index" so that we can generate a deterministic
		// hash representing the cycle, regardless of which node was being processed when the cycle was found.
		// We use this information to not report the same cycle multiple times.

		// Finding the cycle start (the first node that has been encountered more than once)
		// 【中文】找环的起点：第一个在 CycleIndices 中出现 ≥2 次的节点。
		int32 CycleStartElementIndex = INDEX_NONE;
		for (const int32 CycleNodeIndex : CycleIndices)
		{
			if (Algo::Count(CycleIndices, CycleNodeIndex) > 1)
			{
				CycleStartElementIndex = CycleIndices.Find(CycleNodeIndex);
				break;
			}
		}

		// Finding the cycle length by finding the other occurence of cycle-starting node
		// 【中文】环长度 = 起点之后再次出现起点的位置 + 1
		const int32 CycleLength = MakeArrayView(&CycleIndices[CycleStartElementIndex + 1], CycleIndices.Num() - CycleStartElementIndex - 1).Find(CycleIndices[CycleStartElementIndex]) + 1;
		check(CycleLength > 0);

		TConstArrayView<int32> CycleView = MakeArrayView(&CycleIndices[CycleStartElementIndex], CycleLength);
		// Find the deterministic cycle start, only used for hash generation.
		// 【中文】从最小 NodeIndex 开始滚动算 hash → 同一个环用任何起点检测得到相同 hash
		const int32* MinElementIndex = Algo::MinElement(CycleView);
		check(MinElementIndex);
		const int32 LowestCycleElementIndex = CycleView.Find(*MinElementIndex);

		// Calculate cycle's hash
		int32 ElementIndex = LowestCycleElementIndex;
		uint32 CycleHash = static_cast<uint32>(CycleView[ElementIndex++]);
		for (int32 CycleCounter = 1; CycleCounter < CycleView.Num(); ++CycleCounter)
		{
			ElementIndex %= CycleView.Num();
			CycleHash = HashCombine(CycleHash, CycleView[ElementIndex++]);
		}

		// 【中文】仅当之前没报过这个 hash 时才输出 —— 避免同一环刷屏
		if (InOutReportedCycleHashes.Find(CycleHash) == INDEX_NONE)
		{
			InOutReportedCycleHashes.Add(CycleHash);

			UE_LOG(LogMassDependencies, Error, TEXT("Detected processing dependency cycle:"));

			for (const int32 CycleNodeIndex : CycleView)
			{
				if (const UMassProcessor* Processor = AllNodes[CycleNodeIndex].Processor)
				{
					UE_LOG(LogMassDependencies, Warning, TEXT("\t%s, group: %s, before: %s, after %s")
						, *Processor->GetName()
						, *Processor->GetExecutionOrder().ExecuteInGroup.ToString()
						, *NameViewToString(Processor->GetExecutionOrder().ExecuteBefore)
						, *NameViewToString(Processor->GetExecutionOrder().ExecuteAfter));
				}
				else
				{
					// group
					UE_LOG(LogMassDependencies, Warning, TEXT("\tGroup %s"), *AllNodes[CycleNodeIndex].Name.ToString());
				}
			}
		}
	}
#endif // WITH_MASSENTITY_DEBUG

	/**
	 * 【中文】CVar：是否启用 Processor.ExecutionPriority 参与排序。
	 * 关闭后所有节点视为同优先级，仅靠 TotalWaitingNodes 排序。
	 */
	bool bProcessorExecutionPriorityEnabled = true;
	/**
	 * 【中文】CVar：当剩余节点优先级低于队首时，是否拒绝挑选低优先级节点（即使它能"无冲突运行"）。
	 * 启用 → 把高优先级整链整体压前；禁用 → 贪心找无冲突节点（可能让低优先级节点先跑）。
	 */
	bool bPickHigherPriorityNodesRegardlessOfRequirements = true;
	namespace
	{
		FAutoConsoleVariableRef ConsoleVariables[] =
		{
			FAutoConsoleVariableRef(
				TEXT("mass.dependencies.ProcessorExecutionPriorityEnabled"),
				bProcessorExecutionPriorityEnabled,
				TEXT("Controls whether UMassProcessor.ExecutionPriority value is being used during dependency calculations"),
				ECVF_Default)
			, FAutoConsoleVariableRef(
				TEXT("mass.dependencies.PickHigherPriorityNodesRegardlessOfRequirements"),
				bPickHigherPriorityNodesRegardlessOfRequirements,
				TEXT("If enabled, will result in lower priority nodes not being picked, even if they could run without obstructing anything else"),
				ECVF_Default)
		};
	}
}

//----------------------------------------------------------------------//
//  FMassExecutionRequirements
//  【中文】Processor 的"读写指纹" —— 求解器拿这个判断冲突。
//----------------------------------------------------------------------//

/**
 * 把 Other 的所有读写需求合并进来（位集做并集，tag 也并集）。
 * 用途：
 *   - group 节点累积所有子节点的需求；
 *   - FResourceUsage::Requirements 累积所有"已 submit"节点的总需求（用于快速短路）；
 * 注意会把 ResourcesUsedCount 重置为 INDEX_NONE，强制下次 GetTotalBitsUsedCount 时重新统计。
 */
void FMassExecutionRequirements::Append(const FMassExecutionRequirements& Other)
{
	for (int i = 0; i < EMassAccessOperation::MAX; ++i)
	{
		Fragments[i] += Other.Fragments[i];
		ChunkFragments[i] += Other.ChunkFragments[i];
		SharedFragments[i] += Other.SharedFragments[i];
		RequiredSubsystems[i] += Other.RequiredSubsystems[i];
	}
	// 【中文】ConstSharedFragments 只有 Read，特化没有 Write 字段，单独处理
	ConstSharedFragments.Read += Other.ConstSharedFragments.Read;

	RequiredAllTags += Other.RequiredAllTags;
	RequiredAnyTags += Other.RequiredAnyTags;
	RequiredNoneTags += Other.RequiredNoneTags;
	// note that we're deliberately ignoring optional tags, they play no role here.
	// 【中文】Optional tag 只影响 query 是否命中，对调度顺序无影响，故忽略。

	// signal that it requires recalculation;
	// 【中文】缓存失效，等下次 GetTotalBitsUsedCount 重新统计
	ResourcesUsedCount = INDEX_NONE;
}

/**
 * 重新统计 5 类资源（不含 tag）的总位数，结果写入 ResourcesUsedCount。
 * 公式：Σ(每类资源的 Read 位数 + Write 位数) + ConstSharedFragments.Read 位数
 */
void FMassExecutionRequirements::CountResourcesUsed()
{
	ResourcesUsedCount = ConstSharedFragments.Read.CountStoredTypes();

	for (int i = 0; i < EMassAccessOperation::MAX; ++i)
	{
		ResourcesUsedCount += Fragments[i].CountStoredTypes();
		ResourcesUsedCount += ChunkFragments[i].CountStoredTypes();
		ResourcesUsedCount += SharedFragments[i].CountStoredTypes();
		ResourcesUsedCount += RequiredSubsystems[i].CountStoredTypes();
	}
}

/** 资源位 + tag 位的总数。会顺便重新计算 ResourcesUsedCount。 */
int32 FMassExecutionRequirements::GetTotalBitsUsedCount()
{
	CountResourcesUsed();

	return ResourcesUsedCount + RequiredAllTags.CountStoredTypes()
		+ RequiredAnyTags.CountStoredTypes() + RequiredNoneTags.CountStoredTypes();
}

/** 5 类资源 + 3 类 tag 全空才算空。 */
bool FMassExecutionRequirements::IsEmpty() const
{
	return Fragments.IsEmpty() && ChunkFragments.IsEmpty() 
		&& SharedFragments.IsEmpty() && ConstSharedFragments.IsEmpty() && RequiredSubsystems.IsEmpty()
		&& RequiredAllTags.IsEmpty() && RequiredAnyTags.IsEmpty() && RequiredNoneTags.IsEmpty();
}

/**
 * 【中文】把"读写指纹"压平成 archetype 描述符 —— 用于"创建虚拟 archetype 让 query 互相匹配"。
 *
 * 注意 Read 和 Write 在这里被合并：archetype 描述符本身不区分访问模式，它表达的是
 * "这个 archetype 含有哪些 fragment / tag / shared 数据"。
 * 用 Fragments.Read + Fragments.Write 是因为：只要 Processor 声明了对某个 fragment
 * 的任何访问（无论读写），目标 archetype 就必须包含它才能被该 query 匹配到。
 *
 * Tag 部分用 RequiredAllTags + RequiredAnyTags（不含 None） —— 虚拟 archetype 应该
 * 拥有"所有正面要求的 tag"，这样 query 才能匹配。
 */
FMassArchetypeCompositionDescriptor FMassExecutionRequirements::AsCompositionDescriptor() const
{
	return FMassArchetypeCompositionDescriptor(Fragments.Read + Fragments.Write
		, RequiredAllTags + RequiredAnyTags
		, ChunkFragments.Read + ChunkFragments.Write
		, SharedFragments.Read + SharedFragments.Write
		, ConstSharedFragments.Read);
}

//----------------------------------------------------------------------//
//  FProcessorDependencySolver::FResourceUsage
//  【中文】"目前每个资源谁在读 / 谁在写"的运行时账本。
//  求解器一边按拓扑顺序往输出序列里 push 节点（SubmitNode），一边在这本账上
//  登记"现在 NodeX 正在读 FragmentY"。下一个候选节点尝试出列时：
//    - 它要读 Y → 必须依赖目前所有写 Y 的节点
//    - 它要写 Y → 必须依赖目前所有读/写 Y 的节点 → 然后清空它们（自己变新 Writer）
//  这就是"自动避免数据竞争"的核心机制。
//----------------------------------------------------------------------//

/**
 * 构造函数：根据每类资源的最大类型数预分配 Access 表。
 * 例：FMassFragmentBitSet::GetMaxNum() == 注册的所有 Fragment 类型总数 N
 *     → FragmentsAccess.Read.Access 是 N 个 FResourceUsers（每个一开始都是空 Users 列表）
 */
FMassProcessorDependencySolver::FResourceUsage::FResourceUsage(const TArray<FNode>& InAllNodes)
	: AllNodesView(InAllNodes)
{
	for (int i = 0; i < EMassAccessOperation::MAX; ++i)
	{
		FragmentsAccess[i].Access.AddZeroed(FMassFragmentBitSet::GetMaxNum());
		ChunkFragmentsAccess[i].Access.AddZeroed(FMassChunkFragmentBitSet::GetMaxNum());
		SharedFragmentsAccess[i].Access.AddZeroed(FMassSharedFragmentBitSet::GetMaxNum());
		RequiredSubsystemsAccess[i].Access.AddZeroed(FMassExternalSubsystemBitSet::GetMaxNum());
	}
}

/**
 * 【中文】把单个节点对某类资源（Fragment / Subsystem ...）的访问登记到账本上，
 * 并且根据现有 reader/writer 给该节点动态加依赖边。
 *
 * 算法 3 步：
 *   ┌───────────────────────────────────────────────────────────────────┐
 *   │ 1) 对每个"我要读"的位 X：                                            │
 *   │    InOutNode.OriginalDependencies += { 当前正在写 X 的所有节点 }   │
 *   │    （不删除现有 Writer：Reader 不"消费"它）                          │
 *   │                                                                     │
 *   │ 2) 对每个"我要写"的位 X：                                            │
 *   │    InOutNode.OriginalDependencies += { 当前所有读 X 的节点 }       │
 *   │    InOutNode.OriginalDependencies += { 当前所有写 X 的节点 }       │
 *   │    然后从账本中"消费掉"这些 Reader/Writer —— 因为 InOutNode 即将    │
 *   │    成为新的 Writer，后续节点等它就够了                              │
 *   │                                                                     │
 *   │ 3) 把自己登记进 ElementAccess.Read[X] / Write[X] 的 Users 列表       │
 *   └───────────────────────────────────────────────────────────────────┘
 *
 * archetype 优化：当 bSubsystems = false（即操作 fragment 类资源），只有当当前
 * user 节点的 ValidArchetypes 与 InOutNode.ValidArchetypes 重叠时才视为真冲突
 * → 不重叠时既不加边，也不消费它（让其它真正冲突的节点继续依赖它）。
 *
 * @tparam TBitSet 资源类型的位集（FMassFragmentBitSet / FMassExternalSubsystemBitSet ...）
 */
template<typename TBitSet>
void FMassProcessorDependencySolver::FResourceUsage::HandleElementType(TMassExecutionAccess<FResourceAccess>& ElementAccess
	, const TMassExecutionAccess<TBitSet>& TestedRequirements, FMassProcessorDependencySolver::FNode& InOutNode, const int32 NodeIndex)
{
	using UE::Mass::Private::DoArchetypeContainersOverlap;

	// when considering subsystem access we don't care about archetypes, so we cache the information whether
	// we're dealing with subsystems and use that to potentially short-circuit the checks below.
	// 【中文】Subsystem 是全局资源（不归属任何 archetype），故对它跳过 archetype 重叠检查 —— 直接视为冲突。
	constexpr bool bSubsystems = std::is_same_v<TBitSet, FMassExternalSubsystemBitSet>;

	// for every bit set in TestedRequirements we do the following:
	// 1. For every read-only requirement we make InOutNode depend on the currently stored Writer of this resource
	//    - note that this operation is not destructive, meaning we don't destructively consume the data, since all 
	//      subsequent read access to the given resource will also depend on the Writer
	//    - note 2: we also fine tune what we store as a dependency for InOutNode by checking if InOutNode's archetype
	//      overlap with whoever the current Writer is 
	//    - this will result in InOutNode wait for the current Writer to finish before starting its own work and 
	//      that's exactly what we need to do to avoid accessing data while it's potentially being written
	// 2. For every read-write requirement we make InOutNode depend on all the readers and writers currently stored. 
	//    - once that's done we clean currently stored Readers and Writers since every subsequent operation on this 
	//      resource will be blocked by currently considered InOutNode (as the new Writer)
	//    - again, we do check corresponding archetype collections overlap
	//    - similarly to the read operation waiting on write operations in pt 1. we want to hold off the write 
	//      operations to be performed by InOutNode until all currently registered (and conflicting) writers and readers 
	//      are done with their operations 
	// 3. For all accessed resources we store information that InOutNode is accessing it
	//    - we do this so that the following nodes know that they'll have to wait for InOutNode if an access 
	//      conflict arises. 

	// 1. For every read only requirement we make InOutNode depend on the currently stored Writer of this resource
	// 【中文】第 1 步：Read 等 Writer。
	for (auto It = TestedRequirements.Read.GetIndexIterator(); It; ++It)
	{
		for (int32 UserIndex : ElementAccess.Write.Access[*It].Users)
		{
			// 【中文】subsystem 直接加边；fragment 类资源只在 archetype 重叠时加边（精细化剪枝）
			if (bSubsystems || DoArchetypeContainersOverlap(AllNodesView[UserIndex].ValidArchetypes, InOutNode.ValidArchetypes))
			{
				InOutNode.OriginalDependencies.Add(UserIndex);
			}
		}
	}

	// 2. For every read-write requirement we make InOutNode depend on all the readers and writers currently stored. 
	// 【中文】第 2 步：Write 等所有 Reader 和 Writer，并消费它们（让自己成为新 Writer）。
	for (auto It = TestedRequirements.Write.GetIndexIterator(); It; ++It)
	{
		// 【中文】反向遍历是为了 RemoveAtSwap 时不影响后续索引
		for (int32 i = ElementAccess.Read.Access[*It].Users.Num() - 1; i >= 0; --i)
		{
			const int32 UserIndex = ElementAccess.Read.Access[*It].Users[i];
			if (bSubsystems || DoArchetypeContainersOverlap(AllNodesView[UserIndex].ValidArchetypes, InOutNode.ValidArchetypes))
			{	
				InOutNode.OriginalDependencies.Add(UserIndex);
				// 【中文】消费 Reader：之后只要等 InOutNode（新 Writer）就够了
				ElementAccess.Read.Access[*It].Users.RemoveAtSwap(i);
			}
		}

		for (int32 i = ElementAccess.Write.Access[*It].Users.Num() - 1; i >= 0; --i)
		{
			const int32 UserIndex = ElementAccess.Write.Access[*It].Users[i];
			if (bSubsystems || DoArchetypeContainersOverlap(AllNodesView[UserIndex].ValidArchetypes, InOutNode.ValidArchetypes))
			{
				InOutNode.OriginalDependencies.Add(UserIndex);
				// 【中文】消费旧 Writer
				ElementAccess.Write.Access[*It].Users.RemoveAtSwap(i);
			}
		}
	}

	// 3. For all accessed resources we store information that InOutNode is accessing it
	// 【中文】第 3 步：把自己登记到账本里 —— 后来的节点要看到我的存在
	for (auto It = TestedRequirements.Read.GetIndexIterator(); It; ++It)
	{
		// mark Element at index indicated by It as being used in mode EMassAccessOperation(i) by NodeIndex
		ElementAccess.Read.Access[*It].Users.Add(NodeIndex);
	}
	for (auto It = TestedRequirements.Write.GetIndexIterator(); It; ++It)
	{
		// mark Element at index indicated by It as being used in mode EMassAccessOperation(i) by NodeIndex
		ElementAccess.Write.Access[*It].Users.Add(NodeIndex);
	}
}

/**
 * 【中文】纯位集层面的"是否可访问"快速判断 —— 不考虑 archetype。
 * 三个判定条件，任一非空 = 冲突：
 *   1) 我要写的 ∩ 已有写的 ≠ ∅   (W vs W)
 *   2) 我要写的 ∩ 已有读的 ≠ ∅   (W vs R)
 *   3) 我要读的 ∩ 已有写的 ≠ ∅   (R vs W)
 * 注意 Read vs Read 不算冲突（多读并行无问题）。
 *
 * @return true = 可以无冲突访问（无需插入新边）
 */
template<typename TBitSet>
bool FMassProcessorDependencySolver::FResourceUsage::CanAccess(const TMassExecutionAccess<TBitSet>& StoredElements, const TMassExecutionAccess<TBitSet>& TestedElements)
{
	// see if there's an overlap of tested write operations with existing read & write operations, as well as 
	// tested read operations with existing write operations
	
	return !(
		// if someone's already writing to what I want to write
		TestedElements.Write.HasAny(StoredElements.Write)
		// or if someone's already reading what I want to write
		|| TestedElements.Write.HasAny(StoredElements.Read)
		// or if someone's already writing what I want to read
		|| TestedElements.Read.HasAny(StoredElements.Write)
	);
}

/**
 * 【中文】当位集层面已经判出冲突时，再用 archetype 集合"精细化"判断：
 * 如果当前所有 user 的 ValidArchetypes 都与 InArchetypes 不重叠 → 实际上不冲突（放行）
 * 否则 → 真冲突。
 *
 * 这就是"两个 Processor 都写同一类型 Fragment 但操作不同 archetype 仍可并行"的实现。
 *
 * @return true = 有 archetype 重叠 → 真冲突
 */
bool FMassProcessorDependencySolver::FResourceUsage::HasArchetypeConflict(TMassExecutionAccess<FResourceAccess> ElementAccess, const TArray<FMassArchetypeHandle>& InArchetypes) const
{
	using UE::Mass::Private::DoArchetypeContainersOverlap;

	// this function is being run when we've already determined there's an access conflict on given ElementsAccess,
	// meaning whoever's asking is trying to access Elements that are already being used. We can still grant access 
	// though provided that none of the current users of Element access the same archetypes the querier does (as provided 
	// by InArchetypes).
	// @todo this operation could be even more efficient and precise if we tracked which operation (read/write) and which
	// specific Element were conflicting and the we could limit the check to that. That would however significantly 
	// complicate the code and would require a major refactor to keep things clean.
	for (int i = 0; i < EMassAccessOperation::MAX; ++i)
	{
		for (const FResourceUsers& Resource : ElementAccess[i].Access)
		{
			for (const int32 UserIndex : Resource.Users)
			{
				if (DoArchetypeContainersOverlap(AllNodesView[UserIndex].ValidArchetypes, InArchetypes))
				{
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * 【中文】"该候选节点能否当下立即出列（不会与已 submit 节点冲突）？"
 *
 * 对每类资源做：bitset 层面无冲突 OR archetype 层面无重叠 → 视为可访问
 * 注意 ConstSharedFragments 不参与判断 —— 它是只读资源，不可能与谁冲突。
 *
 * @return true 表示可以"干净地"插入；false 表示要么资源冲突要么 archetype 也撞上 →
 *         PerformSolverStep 会把它放入 fallback 候选
 */
bool FMassProcessorDependencySolver::FResourceUsage::CanAccessRequirements(const FMassExecutionRequirements& TestedRequirements, const TArray<FMassArchetypeHandle>& InArchetypes) const
{
	// note that on purpose we're not checking ConstSharedFragments - those are always only read, no danger of conflicting access
	bool bCanAccess = (CanAccess<FMassFragmentBitSet>(Requirements.Fragments, TestedRequirements.Fragments) || !HasArchetypeConflict(FragmentsAccess, InArchetypes))
		&& (CanAccess<FMassChunkFragmentBitSet>(Requirements.ChunkFragments, TestedRequirements.ChunkFragments) || !HasArchetypeConflict(ChunkFragmentsAccess, InArchetypes))
		&& (CanAccess<FMassSharedFragmentBitSet>(Requirements.SharedFragments, TestedRequirements.SharedFragments) || !HasArchetypeConflict(SharedFragmentsAccess, InArchetypes))
		&& CanAccess<FMassExternalSubsystemBitSet>(Requirements.RequiredSubsystems, TestedRequirements.RequiredSubsystems);

	return bCanAccess;
}

/**
 * 【中文】把节点登记到账本（一并向其 OriginalDependencies 追加动态依赖边）。
 *
 * 顺序遍历 4 类资源，每类调一次 HandleElementType：
 *   - Fragments / ChunkFragments / SharedFragments：archetype-aware
 *   - RequiredSubsystems：全局，不考虑 archetype
 * ConstSharedFragments 整体跳过（只读不会冲突）。
 *
 * 最后把节点的 Requirements 累计到 Requirements 字段，让 CanAccessRequirements 的
 * 位集快速短路判断生效。
 */
void FMassProcessorDependencySolver::FResourceUsage::SubmitNode(const int32 NodeIndex, FNode& InOutNode)
{
	HandleElementType<FMassFragmentBitSet>(FragmentsAccess, InOutNode.Requirements.Fragments, InOutNode, NodeIndex);
	HandleElementType<FMassChunkFragmentBitSet>(ChunkFragmentsAccess, InOutNode.Requirements.ChunkFragments, InOutNode, NodeIndex);
	HandleElementType<FMassSharedFragmentBitSet>(SharedFragmentsAccess, InOutNode.Requirements.SharedFragments, InOutNode, NodeIndex);
	HandleElementType<FMassExternalSubsystemBitSet>(RequiredSubsystemsAccess, InOutNode.Requirements.RequiredSubsystems, InOutNode, NodeIndex);
	// note that on purpose we're not pushing ConstSharedFragments - those are always only read, no danger of conflicting access
	// so there's no point in tracking them

	Requirements.Append(InOutNode.Requirements);
}

//----------------------------------------------------------------------//
//  FProcessorDependencySolver::FNode
//  【中文】求解器节点 —— Processor 或 group 的内部表示
//----------------------------------------------------------------------//

/**
 * 【中文】递归向上 +1 计数 "有多少节点在等我"。
 *
 * 算法（DFS over OriginalDependencies）：
 *   func(self):
 *     ++self.TotalWaitingNodes
 *     for dep in self.OriginalDependencies:
 *       func(dep)
 *
 * 起调位置：Solve() 对每个非 group 节点都调一次 → 每经过一个节点 +1 →
 *   TotalWaitingNodes 反映 "整个图里有多少条最终汇聚到我的路径"。
 *
 * 环检测：IterationsLimit 一开始 = 总依赖节点数（最深可能链长度），
 *   每深入一层 -1，<0 即说明走了一个环 → 把途经节点 push 进 OutCycleIndices 并返回 false。
 *
 * @return false 表示发现了环
 */
bool FMassProcessorDependencySolver::FNode::IncreaseWaitingNodesCount(TArrayView<FMassProcessorDependencySolver::FNode> InAllNodes
	, const int32 IterationsLimit, TArray<int32>& OutCycleIndices)
{
	// cycle-protection check. If true it means we have a cycle and the whole algorithm result will be unreliable 
	if (IterationsLimit < 0)
	{
		OutCycleIndices.Add(NodeIndex);
		return false;
	}

	++TotalWaitingNodes;

	for (const int32 DependencyIndex : OriginalDependencies)
	{
		check(&InAllNodes[DependencyIndex] != this);
		if (InAllNodes[DependencyIndex].IncreaseWaitingNodesCount(InAllNodes, IterationsLimit - 1, OutCycleIndices) == false)
		{
			// 【中文】下游报告环，把自己也加进去 —— 形成完整的环节点列表（自下而上）
			OutCycleIndices.Add(NodeIndex);
			return false;
		}
	}

	return true;
}

/**
 * 【中文】带优先级传染的版本。
 *
 * 在 IncreaseWaitingNodesCount 基础上额外做：
 *   - 用 InChildPriority（来自调用者）更新自己的 MaxExecutionPriority（取 max，公式 +1）
 *   - 把更新后的 MaxExecutionPriority 作为 ChildPriority 传给上游父节点
 *
 * 效果：高优先级节点会沿依赖图向上"传染"，最终所有它的祖先节点都拿到一个被抬高过
 * 的 MaxExecutionPriority。Solve 排序时把 MaxExecutionPriority 当首要排序键 →
 * 整条高优先级链被压到最前面。
 *
 * +1 是为了保证"被依赖者优先级 ≥ 依赖者优先级 + 1"，多次合并不会漏掉链中较低的祖先。
 */
bool FMassProcessorDependencySolver::FNode::IncreaseWaitingNodesCountAndPriority(TArrayView<FNode> InAllNodes, const int32 IterationsLimit
	, TArray<int32>& OutCycleIndices, const int32 InChildPriority)
{
	// cycle-protection check. If true it means we have a cycle and the whole algorithm result will be unreliable 
	if (IterationsLimit < 0)
	{
		OutCycleIndices.Add(NodeIndex);
		return false;
	}

	++TotalWaitingNodes;
	UpdateExecutionPriority(InChildPriority);

	for (const int32 DependencyIndex : OriginalDependencies)
	{
		check(&InAllNodes[DependencyIndex] != this);
		// 【中文】把自己更新后的 MaxExecutionPriority 传给父节点 —— 优先级一路上升
		if (InAllNodes[DependencyIndex].IncreaseWaitingNodesCountAndPriority(InAllNodes, IterationsLimit - 1, OutCycleIndices, MaxExecutionPriority) == false)
		{
			OutCycleIndices.Add(NodeIndex);
			return false;
		}
	}

	return true;
}
//----------------------------------------------------------------------//
//  FProcessorDependencySolver
//  【中文】调度求解器主体
//----------------------------------------------------------------------//

/**
 * 构造函数：仅做参数绑定，不做任何工作（求解发生在 ResolveDependencies 中）。
 * @param InProcessors    Processor 视图，调用者保证生命周期跨过 ResolveDependencies
 * @param bIsGameRuntime  true = 真实运行时；false = 编辑器/调试，不允许 query-based pruning
 */
FMassProcessorDependencySolver::FMassProcessorDependencySolver(TArrayView<UMassProcessor* const> InProcessors, const bool bIsGameRuntime)
	: Processors(InProcessors)
	, bGameRuntime(bIsGameRuntime)
{}

/**
 * 【中文】Solve 主循环的"一步"：在剩余候选中挑出一个最合适的节点出列。
 *
 * 算法（从前往后扫一遍 InOutIndicesRemaining）：
 *   ┌──────────────────────────────────────────────────────────────────┐
 *   │ HighestPriority = AllNodes[InOutIndicesRemaining[0]].MaxExecutionPriority │
 *   │ for each candidate i in InOutIndicesRemaining:                    │
 *   │   if candidate.TransientDependencies != empty: skip               │
 *   │   if 单线程目标 OR ResourceUsage 接受它:                            │
 *   │     AcceptedNodeIndex = candidate; break (最优解)                  │
 *   │   else:                                                            │
 *   │     第一次遇到资源冲突 → FallbackAcceptedNodeIndex = candidate     │
 *   │     若开启"PickHigherPriorityNodesRegardlessOfRequirements"        │
 *   │     且 candidate.Priority < HighestPriority:                       │
 *   │       break（不再贪心找完美匹配，直接用 fallback 保持高优先级）    │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * 出列后副作用（4 件事）：
 *   1) ResourceUsage.SubmitNode → 给该节点加动态依赖边并登记 reader/writer
 *   2) 从 InOutIndicesRemaining 移除
 *   3) SequencePositionIndex = max(deps.Seq) + 1（这是 ParallelDispatch 的层数）
 *   4) 从所有剩余节点的 TransientDependencies 中移除自己（解锁后继）
 *
 * @return false 表示连 fallback 都没有 —— 全场剩余节点都还有 transient 依赖未满足
 *               → Solve 主循环判定为环，强制断一条边再试
 */
bool FMassProcessorDependencySolver::PerformSolverStep(FResourceUsage& ResourceUsage, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices)
{
	int32 AcceptedNodeIndex = INDEX_NONE;
	int32 FallbackAcceptedNodeIndex = INDEX_NONE;

	// 【中文】队首 = 当前剩余里优先级最高的（IndicesRemaining 已按优先级降序排过）
	const int32 HighestPriority = AllNodes[InOutIndicesRemaining[0]].MaxExecutionPriority;
	for (int32 i = 0; i < InOutIndicesRemaining.Num(); ++i)
	{
		const int32 NodeIndex = InOutIndicesRemaining[i];
		if (AllNodes[NodeIndex].TransientDependencies.Num() == 0)
		{
			// if we're solving dependencies for a single thread use we don't need to fine-tune the order based on resources nor archetypes
			// 【中文】单线程模式：完全跳过资源/archetype 冲突检查 —— 反正都串行，不会有 race
			if (bSingleThreadTarget || ResourceUsage.CanAccessRequirements(AllNodes[NodeIndex].Requirements, AllNodes[NodeIndex].ValidArchetypes))
			{
				AcceptedNodeIndex = NodeIndex;
				break;
			}
			else if (FallbackAcceptedNodeIndex == INDEX_NONE)
			{
				// if none of the nodes left can "cleanly" execute (i.e. without conflicting with already stored nodes)
				// we'll just pick this one up and go with it. 
				// 【中文】记下第一个"依赖空但有资源冲突"的候选 —— 作为最坏情况回退
				FallbackAcceptedNodeIndex = NodeIndex;
			}
			else if (UE::Mass::Private::bPickHigherPriorityNodesRegardlessOfRequirements 
				&& AllNodes[NodeIndex].MaxExecutionPriority < HighestPriority)
			{
				// subsequent nodes are of lower execution priority, we break now and will use FallbackAcceptedNodeIndex
				// 【中文】既然之后都是更低优先级的，没必要继续扫；直接用 fallback 保持高优先级链整体压前
				check(FallbackAcceptedNodeIndex != INDEX_NONE);
				checkf(UE::Mass::Private::bProcessorExecutionPriorityEnabled == true
					, TEXT("We never expect to hit this case when execution priorities are disabled - all nodes should have the same priority."))
				break;
			}
		}
	}

	if (AcceptedNodeIndex != INDEX_NONE || FallbackAcceptedNodeIndex != INDEX_NONE)
	{
		const int32 NodeIndex = AcceptedNodeIndex != INDEX_NONE ? AcceptedNodeIndex : FallbackAcceptedNodeIndex;

		FNode& Node = AllNodes[NodeIndex];

		// Note that this is not an unexpected event and will happen during every dependency solving. It's a part 
		// of the algorithm. We initially look for all the things we can run without conflicting with anything else. 
		// But that can't last forever, at some point we'll end up in a situation where every node left waits for 
		// something that has been submitted already. Then we just pick one of the waiting ones (the one indicated by 
		// FallbackAcceptedNodeIndex), "run it" and proceed.
		// 【中文】使用 fallback 是正常现象：一开始能找完美匹配，到后面只能"挑一个排在前面的"，
		//         此时插入资源依赖边，逻辑上等于"等前面节点跑完再跑"，仍然正确。
		UE_CLOG(AcceptedNodeIndex == INDEX_NONE, LogMassDependencies, Verbose, TEXT("No dependency-free node can be picked, due to resource requirements. Picking %s as the next node.")
			, *Node.Name.ToString());

		// 【中文】登记到账本（这里会动态加 OriginalDependencies 边）
		ResourceUsage.SubmitNode(NodeIndex, Node);
		InOutIndicesRemaining.RemoveSingle(NodeIndex);
		OutNodeIndices.Add(NodeIndex);
		// 【中文】SequencePositionIndex = 所有依赖的最大层数 + 1
		//         决定 ParallelDispatch 时这个节点要等到第几个 fence
		for (const int32 DependencyIndex : Node.OriginalDependencies)
		{
			Node.SequencePositionIndex = FMath::Max(Node.SequencePositionIndex, AllNodes[DependencyIndex].SequencePositionIndex);
		}
		++Node.SequencePositionIndex;

		// 【中文】解锁后继：所有剩余节点的 TransientDependencies 中删除自己
		for (const int32 RemainingNodeIndex : InOutIndicesRemaining)
		{
			AllNodes[RemainingNodeIndex].TransientDependencies.RemoveSingleSwap(NodeIndex, EAllowShrinking::No);
		}
		
		return true;
	}

	return false;
}

/**
 * 【中文】把 "A.B.C" 形式的层级 group 名拆成累进数组：["A", "A.B", "A.B.C"]。
 * CreateNodes 会按这个顺序自顶向下查找/创建 group 节点 → 形成 group 嵌套树。
 *
 * 算法：先按 "." 切成 ["A","B","C"]，然后从 i=1 开始累加前缀。
 */
void FMassProcessorDependencySolver::CreateSubGroupNames(FName InGroupName, TArray<FString>& SubGroupNames)
{
	// the function will convert composite group name into a series of progressively more precise group names
	// so "A.B.C" will result in ["A", "A.B", "A.B.C"]

	SubGroupNames.Reset();
	FString GroupNameAsString = InGroupName.ToString();
	FString TopGroupName;

	while (GroupNameAsString.Split(TEXT("."), &TopGroupName, &GroupNameAsString))
	{
		SubGroupNames.Add(TopGroupName);
	}
	SubGroupNames.Add(GroupNameAsString);
	
	// 【中文】把 ["A","B","C"] 累进成 ["A","A.B","A.B.C"]
	for (int i = 1; i < SubGroupNames.Num(); ++i)
	{
		SubGroupNames[i] = FString::Printf(TEXT("%s.%s"), *SubGroupNames[i - 1], *SubGroupNames[i]);
	}
}

/**
 * 【中文】为单个 Processor 创建节点，并自动构建/复用 group 节点树。
 *
 * 流程：
 *   1) 选择查找名 ProcName：
 *      - ShouldAllowMultipleInstances → 用 Processor 的 instance FName（用户必须保证唯一）
 *      - 否则 → 用 Processor 的 Class FName（同类只允许一个）
 *   2) 若 NodeIndexMap 已经存在 ProcName → 警告并返回旧索引
 *   3) 解析 ExecuteInGroup（"A.B.C"）→ 自顶向下找/建 group 节点链
 *      每个 group 节点都依赖它的父 group 节点（OriginalDependencies），
 *      父 group 把子 group 加到 SubNodeIndices
 *   4) 创建 Processor 节点：
 *      - 拷贝 ExecuteBefore / ExecuteAfter
 *      - 调 Processor.ExportRequirements 取出读写指纹
 *      - 把 MultiThreadedSystemsBitSet 中的 subsystem 从 Read/Write 中减掉
 *        （那些 subsystem 自己处理并发，调度器不必为它们插入边）
 *      - 设置 MaxExecutionPriority（受 cvar 控制是否启用）
 *   5) 把 Processor 节点登记为父 group 的 SubNode
 *
 * @return Processor 节点的 NodeIndex
 */
int32 FMassProcessorDependencySolver::CreateNodes(UMassProcessor& Processor)
{
	check(Processor.GetClass());
	// for processors supporting multiple instances we use processor name rather than processor's class name for
	// dependency calculations. This makes the user responsible for fine-tuning per-processor dependencies. 
	const FName ProcName = Processor.ShouldAllowMultipleInstances() 
		? Processor.GetFName()
		: Processor.GetClass()->GetFName();

	if (const int32* NodeIndexPtr = NodeIndexMap.Find(ProcName))
	{
		// 【中文】已经创建过同名节点 —— 区分两种情况打不同警告
		if (Processor.ShouldAllowMultipleInstances())
		{
			UE_LOG(LogMassDependencies, Warning, TEXT("%hs Processor %s, name %s, already registered. This processor class does suport duplicates, but individual instances need to have a unique name.")
				, __FUNCTION__, *Processor.GetFullName(), *ProcName.ToString());
		}
		else
		{
			UE_LOG(LogMassDependencies, Warning, TEXT("%hs Processor %s already registered. Duplicates are not supported by this processor class.")
				, __FUNCTION__, *ProcName.ToString());
		}
		return *NodeIndexPtr;
	}

	const FMassProcessorExecutionOrder& ExecutionOrder = Processor.GetExecutionOrder();

	// first figure out the groups so that the group nodes come before the processor nodes, this is required for child
	// nodes to inherit group's dependencies like in scenarios where some processor required to ExecuteBefore a given group
	// 【中文】先建 group 节点（必须在 processor 节点之前创建），这样后面 BuildDependencies
	//         能让 group 的依赖正确地往子节点传递
	int32 ParentGroupNodeIndex = INDEX_NONE;
	if (ExecutionOrder.ExecuteInGroup.IsNone() == false)
	{
		TArray<FString> AllGroupNames;
		CreateSubGroupNames(ExecutionOrder.ExecuteInGroup, AllGroupNames);
	
		check(AllGroupNames.Num() > 0);

		// 【中文】自顶向下为每一级 group 找/建节点；新 group 自动依赖上一级 group
		for (const FString& GroupName : AllGroupNames)
		{
			const FName GroupFName(GroupName);
			int32* LocalGroupIndex = NodeIndexMap.Find(GroupFName);
			// group name hasn't been encountered yet - create it
			if (LocalGroupIndex == nullptr)
			{
				int32 NewGroupNodeIndex = AllNodes.Num();
				NodeIndexMap.Add(GroupFName, NewGroupNodeIndex);
				FNode& GroupNode = AllNodes.Add_GetRef({ GroupFName, nullptr, NewGroupNodeIndex });
				// just ignore depending on the dummy "root" node
				// 【中文】顶层 group 不需要依赖 root（root 仅做命名空间锚点）
				if (ParentGroupNodeIndex != INDEX_NONE)
				{
					GroupNode.OriginalDependencies.Add(ParentGroupNodeIndex);
					AllNodes[ParentGroupNodeIndex].SubNodeIndices.Add(NewGroupNodeIndex);
				}

				ParentGroupNodeIndex = NewGroupNodeIndex;
			}
			else
			{	
				ParentGroupNodeIndex = *LocalGroupIndex;
			}

		}
	}

	const int32 NodeIndex = AllNodes.Num();
	NodeIndexMap.Add(ProcName, NodeIndex);
	FNode& ProcessorNode = AllNodes.Add_GetRef({ ProcName, &Processor, NodeIndex });

	ProcessorNode.ExecuteAfter.Append(ExecutionOrder.ExecuteAfter);
	ProcessorNode.ExecuteBefore.Append(ExecutionOrder.ExecuteBefore);
	// 【中文】抽取 Processor 自己声明的读写需求（基于其 EntityQuery / Subsystem 依赖）
	Processor.ExportRequirements(ProcessorNode.Requirements);
	// we're clearing out information about the thread-safe subsystems since
	// we don't need to consider them while tracking subsystem access for thread-safety purposes
	// 【中文】把"线程安全可并写"的 subsystem 从需求中剥离 —— 它们对求解器透明
	ProcessorNode.Requirements.RequiredSubsystems.Write -= MultiThreadedSystemsBitSet;
	ProcessorNode.Requirements.RequiredSubsystems.Read -= MultiThreadedSystemsBitSet;
	ProcessorNode.Requirements.CountResourcesUsed();
	ProcessorNode.MaxExecutionPriority = UE::Mass::Private::bProcessorExecutionPriorityEnabled
		? Processor.GetExecutionPriority()
		: 0;

	// 【中文】把自己挂到父 group 的 SubNode 列表（注意 0 是 root，跳过）
	// @注意：这里写的是 > 0，root（index 0）的 SubNodeIndices 不会被填充 —— 是设计选择，
	//        因为 BuildDependencies 中 group 依赖向下传时不希望从 root 开始扩散
	if (ParentGroupNodeIndex > 0)
	{
		AllNodes[ParentGroupNodeIndex].SubNodeIndices.Add(NodeIndex);
	}

	return NodeIndex;
}

/**
 * 【中文】把"用户声明的字符串依赖"翻译成"指针/索引依赖"。
 *
 * 第一阶段（按 ExecuteBefore 翻转）：
 *   节点 A 声明 "ExecuteBefore B" 等价于 "B ExecuteAfter A"
 *   → 把 A 的名字加到 B 的 ExecuteAfter，A 自己的 ExecuteBefore 清空
 *   → 后续只需处理 ExecuteAfter
 *
 * 第二阶段（按 ExecuteAfter 解析为 OriginalDependencies）：
 *   节点 X 声明 "ExecuteAfter Y"：
 *     - Y 是 processor → X.OriginalDependencies += YIndex
 *     - Y 是 group     → 把 Y 的所有 SubNode 加到 X.ExecuteAfter（继续展开）
 *
 * 第三阶段（group 依赖向下传给子节点）：
 *   group G 依赖于 processor P → G 的所有子节点也都依赖 P
 *   group G 依赖于另一个 group  → G 的所有子节点都加到 ExecuteAfter（再次展开）
 *
 * 缺失的依赖名会被建一个"dummy group 节点"，保证传递性依然有效（如 A→C, B→C，
 * 即使 C 不存在仍能让 A、B 排好相对顺序）。
 */
void FMassProcessorDependencySolver::BuildDependencies()
{
	// at this point we have collected all the known processors and groups in AllNodes so we can transpose 
	// A.ExecuteBefore(B) type of dependencies into B.ExecuteAfter(A)
	// 【中文】第一阶段：ExecuteBefore → 对方的 ExecuteAfter
	for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); ++NodeIndex)
	{
		for (int i = 0; i < AllNodes[NodeIndex].ExecuteBefore.Num(); ++i)
		{
			const FName BeforeDependencyName = AllNodes[NodeIndex].ExecuteBefore[i];
			int32 DependentNodeIndex = INDEX_NONE;
			int32* DependentNodeIndexPtr = NodeIndexMap.Find(BeforeDependencyName);
			if (DependentNodeIndexPtr == nullptr)
			{
				// missing dependency. Adding a "dummy" node representing those to still support ordering based on missing groups or processors 
				// For example, if Processor A and B declare dependency, respectively, "Before C" and "After C" we still 
				// expect A to come before B regardless of whether C exists or not.
				// 【中文】缺失依赖：建 dummy group 节点（Processor=null）作为中转，
				//         让"A→dummyC→B"的传递依赖仍然成立
				
				DependentNodeIndex = AllNodes.Num();
				NodeIndexMap.Add(BeforeDependencyName, DependentNodeIndex);
				AllNodes.Add({ BeforeDependencyName, nullptr, DependentNodeIndex });

				UE_LOG(LogMassDependencies, Log, TEXT("Unable to find dependency \"%s\" declared by %s. Creating a dummy dependency node.")
					, *BeforeDependencyName.ToString(), *AllNodes[NodeIndex].Name.ToString());
			}
			else
			{
				DependentNodeIndex = *DependentNodeIndexPtr;
			}

			check(AllNodes.IsValidIndex(DependentNodeIndex));
			AllNodes[DependentNodeIndex].ExecuteAfter.Add(AllNodes[NodeIndex].Name);
		}
		AllNodes[NodeIndex].ExecuteBefore.Reset();
	}

	// at this point all nodes contain:
	// - single "original dependency" pointing at its parent group
	// - ExecuteAfter populated with node names

	// Now, for every Name in ExecuteAfter we do the following:
	//	if Name represents a processor, add it as "original dependency"
	//	else, if Name represents a group:
	//		- append all group's child node names to ExecuteAfter
	// 
	// 【中文】第二阶段：ExecuteAfter → OriginalDependencies。
	//          group 依赖会被展开成子节点级别的依赖。
	for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); ++NodeIndex)
	{
		// 【中文】注意这里循环条件是 .Num() —— 循环体可能往 ExecuteAfter 添加新元素（group 展开），
		//         循环要继续处理新元素，故不能缓存初始 Num
		for (int i = 0; i < AllNodes[NodeIndex].ExecuteAfter.Num(); ++i)
		{
			const FName AfterDependencyName = AllNodes[NodeIndex].ExecuteAfter[i];
			int32* PrerequisiteNodeIndexPtr = NodeIndexMap.Find(AfterDependencyName);
			int32 PrerequisiteNodeIndex = INDEX_NONE;

			if (PrerequisiteNodeIndexPtr == nullptr)
			{
				// missing dependency. Adding a "dummy" node representing those to still support ordering based on missing groups or processors 
				// For example, if Processor A and B declare dependency, respectively, "Before C" and "After C" we still 
				// expect A to come before B regardless of whether C exists or not.

				PrerequisiteNodeIndex = AllNodes.Num();
				NodeIndexMap.Add(AfterDependencyName, PrerequisiteNodeIndex);
				AllNodes.Add({ AfterDependencyName, nullptr, PrerequisiteNodeIndex });

				UE_LOG(LogMassDependencies, Log, TEXT("Unable to find dependency \"%s\" declared by %s. Creating a dummy dependency node.")
					, *AfterDependencyName.ToString(), *AllNodes[NodeIndex].Name.ToString());
			}
			else
			{
				PrerequisiteNodeIndex = *PrerequisiteNodeIndexPtr;
			}

			const FNode& PrerequisiteNode = AllNodes[PrerequisiteNodeIndex];

			if (PrerequisiteNode.IsGroup())
			{
				// 【中文】"我在 group X 之后" → 我必须在 X 的所有子节点之后
				//         注意 AddUnique 防止子节点中有同名导致重复
				for (int32 SubNodeIndex : PrerequisiteNode.SubNodeIndices)
				{
					AllNodes[NodeIndex].ExecuteAfter.AddUnique(AllNodes[SubNodeIndex].Name);
				}
			}
			else
			{
				// 【中文】Processor 节点 → 直接转 OriginalDependencies（按索引）
				AllNodes[NodeIndex].OriginalDependencies.AddUnique(PrerequisiteNodeIndex);
			}
		}

		// if this node is a group push all the dependencies down on all the children
		// by design all child nodes come after group nodes so the child nodes' dependencies have not been processed yet
		// 【中文】第三阶段：group 节点的依赖向下传给所有子节点。
		//         由于 CreateNodes 保证 group 节点 NodeIndex < 子 processor 节点 NodeIndex，
		//         本循环遍历到 group 时它的子节点尚未处理 ExecuteAfter，故此处下放是安全的。
		if (AllNodes[NodeIndex].IsGroup() && AllNodes[NodeIndex].SubNodeIndices.Num())
		{
			for (int32 PrerequisiteNodeIndex : AllNodes[NodeIndex].OriginalDependencies)
			{
				checkSlow(PrerequisiteNodeIndex != NodeIndex);
				// in case of processor nodes we can store it directly
				if (AllNodes[PrerequisiteNodeIndex].IsGroup() == false)
				{
					// 【中文】依赖目标是 processor → 子节点都按索引直接加
					for (int32 ChildNodeIndex : AllNodes[NodeIndex].SubNodeIndices)
					{
						AllNodes[ChildNodeIndex].OriginalDependencies.AddUnique(PrerequisiteNodeIndex);
					}
				}
				// special case - if dependency is a group and we haven't processed that group yet, we need to add it by name
				else if (PrerequisiteNodeIndex > NodeIndex)
				{
					// 【中文】依赖目标是后面会处理的 group → 按名字塞到子节点的 ExecuteAfter，
					//         等子节点循环处理到时再展开（避免提前展开漏掉后加进来的子节点）
					const FName& PrerequisiteName = AllNodes[PrerequisiteNodeIndex].Name;
					for (int32 ChildNodeIndex : AllNodes[NodeIndex].SubNodeIndices)
					{
						AllNodes[ChildNodeIndex].ExecuteAfter.AddUnique(PrerequisiteName);
					}
				}
				// 【注意：PrerequisiteNodeIndex < NodeIndex 且为 group 的情况没有处理 —— 见总结】
			}
		}
	}
}

/**
 * 调试用：递归打印整棵节点树，按 group 嵌套缩进。
 * 注意 group 节点的 ExecuteBefore/After 来自 FNode 自身字段，processor 节点则
 * 从 Processor->GetExecutionOrder 实时取（更接近用户原始声明）。
 */
void FMassProcessorDependencySolver::LogNode(const FNode& Node, int Indent)
{
	using UE::Mass::Private::NameViewToString;

	if (Node.IsGroup())
	{
		UE_LOG(LogMassDependencies, Log, TEXT("%*s%s before:%s after:%s"), Indent, TEXT(""), *Node.Name.ToString()
			, *NameViewToString(Node.ExecuteBefore)
			, *NameViewToString(Node.ExecuteAfter));

		for (const int32 NodeIndex : Node.SubNodeIndices)
		{
			LogNode(AllNodes[NodeIndex], Indent + 4);
		}
	}
	else
	{
		CA_ASSUME(Node.Processor); // as implied by Node.IsGroup() == false
		UE_LOG(LogMassDependencies, Log, TEXT("%*s%s before:%s after:%s"), Indent, TEXT(""), *Node.Name.ToString()
			, *NameViewToString(Node.Processor->GetExecutionOrder().ExecuteBefore)
			, *NameViewToString(Node.Processor->GetExecutionOrder().ExecuteAfter));
	}
}

/**
 * 【中文】拓扑排序 + 资源冲突感知的"求解主循环"。
 *
 * 步骤：
 *   1) 准备：
 *      - 把 OriginalDependencies 拷贝到 TransientDependencies（求解过程会扣减它）
 *      - 统计 TotalDependingNodes（有依赖的节点数）—— 用作 IncreaseWaitingNodes 的环检测预算
 *
 *   2) DFS 累计 TotalWaitingNodes（"有多少节点在等我"）。
 *      根据 cvar bProcessorExecutionPriorityEnabled 选两个版本之一：
 *      - 启用 → IncreaseWaitingNodesCountAndPriority（同时传染 MaxExecutionPriority）
 *      - 禁用 → IncreaseWaitingNodesCount
 *      DFS 中检测到环 → LogCycle，但不立刻终止（继续扫，可能多个环）
 *
 *   3) 把所有非 group 节点放进 IndicesRemaining，按以下排序：
 *      - 优先级版：先按 MaxExecutionPriority 降序，相同则按 TotalWaitingNodes 降序
 *      - 普通版  ：仅按 TotalWaitingNodes 降序
 *      → 排在前面的节点会优先被尝试出列
 *
 *   4) 主循环：while IndicesRemaining 非空，调 PerformSolverStep
 *      - 成功 → 一节点出列，TransientDependencies 联动更新
 *      - 失败 → 全场都还有 transient 依赖 = 还存在环 → 强制断 IndicesRemaining[0]
 *               的最后一条 transient 依赖（同时也从 OriginalDependencies 删，避免最终
 *               输出还带环）
 *
 *   5) 把 SortedNodeIndices 按顺序填到 OutResult，并附上每个节点的依赖名列表。
 */
void FMassProcessorDependencySolver::Solve(TArray<FMassProcessorOrderInfo>& OutResult)
{
	using UE::Mass::Private::NameViewToString;

	if (AllNodes.Num() == 0)
	{
		return;
	}

	// for more efficient cycle detection and breaking it will be useful to know how many
	// nodes do not depend on anything - we can use this number as a limit for the longest dependency chain
	// 【中文】TotalDependingNodes = "有依赖的节点数" 的上界 → 这是任何合法依赖链的最大可能长度，
	//         超过它就一定走了环。用作 IncreaseWaitingNodesCount 的 IterationsLimit。
	int32 TotalDependingNodes = 0;

	for (FNode& Node : AllNodes)
	{
		Node.TransientDependencies = Node.OriginalDependencies;
		Node.TotalWaitingNodes = 0;
		TotalDependingNodes += (Node.OriginalDependencies.Num() > 0) ? 1 : 0;
	}

	TArray<int32> CycleIndices;
#if WITH_MASSENTITY_DEBUG
	// 【中文】记录已经报过的环的 hash —— 同一个环不重复刷屏
	TArray<uint32> ReportedCycleHashes;
#endif // WITH_MASSENTITY_DEBUG

	TArray<int32> IndicesRemaining;
	// @todo code duplication in this if-else block is temporary, will be reduced to one or the other
	// once the bProcessorExecutionPriorityEnabled feature is accepted or removed. 
	// 【中文】两套代码几乎一样，只差是否传染 priority。Epic 注释说待该 feature 稳定后会合并。
	if (UE::Mass::Private::bProcessorExecutionPriorityEnabled)
	{
		IndicesRemaining.Reserve(AllNodes.Num());
		for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); ++NodeIndex)
		{
			// skip all the group nodes, all group dependencies have already been converted to individual processor dependencies
			// 【中文】group 节点已经把依赖下放给子节点了，求解时只关心 processor 节点
			if (AllNodes[NodeIndex].IsGroup() == false)
			{
				IndicesRemaining.Add(NodeIndex);
				if (AllNodes[NodeIndex].IncreaseWaitingNodesCountAndPriority(AllNodes, TotalDependingNodes, CycleIndices) == false)
				{
#if WITH_MASSENTITY_DEBUG
					// we have a cycle. Report it here
					UE::Mass::Private::LogCycle(AllNodes, CycleIndices, ReportedCycleHashes);
#endif // WITH_MASSENTITY_DEBUG
					CycleIndices.Reset();
				}
			}
		}

		// 【中文】关键排序：高优先级 + 多人等 → 排到前面，PerformSolverStep 优先尝试它们
		IndicesRemaining.Sort([this](const int32 IndexA, const int32 IndexB){
			return AllNodes[IndexA].MaxExecutionPriority > AllNodes[IndexB].MaxExecutionPriority
				|| (AllNodes[IndexA].MaxExecutionPriority == AllNodes[IndexB].MaxExecutionPriority
					&& AllNodes[IndexA].TotalWaitingNodes > AllNodes[IndexB].TotalWaitingNodes);
		});
	}
	else
	{
		IndicesRemaining.Reserve(AllNodes.Num());
		for (int32 NodeIndex = 0; NodeIndex < AllNodes.Num(); ++NodeIndex)
		{
			// skip all the group nodes, all group dependencies have already been converted to individual processor dependencies
			if (AllNodes[NodeIndex].IsGroup() == false)
			{
				IndicesRemaining.Add(NodeIndex);
				if (AllNodes[NodeIndex].IncreaseWaitingNodesCount(AllNodes, TotalDependingNodes, CycleIndices) == false)
				{
#if WITH_MASSENTITY_DEBUG
					// we have a cycle. Report it here
					UE::Mass::Private::LogCycle(AllNodes, CycleIndices, ReportedCycleHashes);
#endif // WITH_MASSENTITY_DEBUG
					CycleIndices.Reset();
				}
			}
		}

		IndicesRemaining.Sort([this](const int32 IndexA, const int32 IndexB){
			return AllNodes[IndexA].TotalWaitingNodes > AllNodes[IndexB].TotalWaitingNodes;
		});
	}

	// this is where we'll be tracking what's being accessed by whom
	// 【中文】资源访问账本 —— 求解过程中持续追踪 reader/writer
	FResourceUsage ResourceUsage(AllNodes);

	TArray<int32> SortedNodeIndices;
	SortedNodeIndices.Reserve(AllNodes.Num());

	while (IndicesRemaining.Num())
	{
		const bool bStepSuccessful = PerformSolverStep(ResourceUsage, IndicesRemaining, SortedNodeIndices);

		if (bStepSuccessful == false)
		{
			// 【中文】所有剩余节点都还有 transient 依赖 → 一定有环 → 强行断一条边继续。
			//         这是兜底措施：环本应在前面 IncreaseWaitingNodes 阶段被检测出，
			//         但即使被检测出来这里也会再来一次，因为我们必须真的把序列输出出来。
			UE_LOG(LogMassDependencies, Error, TEXT("Encountered processing dependency cycle - cutting the chain at an arbitrary location."));

			// remove first dependency
			// note that if we're in a cycle handling scenario every node does have some dependencies left
			// 【中文】Pop 拿最后一条 transient 依赖（仅在环里时才会到这里）
			const int32 DependencyNodeIndex = AllNodes[IndicesRemaining[0]].TransientDependencies.Pop(EAllowShrinking::No);
			// we need to remove this dependency from original dependencies as well, otherwise we'll still have the cycle
			// in the data being produces as a result of the whole algorithm
			// 【中文】不光从 transient 删，还要从 original 删 —— 否则最终输出的依赖名列表还会带环
			AllNodes[IndicesRemaining[0]].OriginalDependencies.Remove(DependencyNodeIndex);
		}
	}

	// now we have the desired order in SortedNodeIndices. We have to traverse it to add to OutResult
	// 【中文】把排好序的 NodeIndex 转成对外的 FMassProcessorOrderInfo 列表
	for (int i = 0; i < SortedNodeIndices.Num(); ++i)
	{
		const int32 NodeIndex = SortedNodeIndices[i];

		TArray<FName> DependencyNames;
		for (const int32 DependencyIndex : AllNodes[NodeIndex].OriginalDependencies)
		{
			DependencyNames.AddUnique(AllNodes[DependencyIndex].Name);
		}

		// at this point we expect SortedNodeIndices to only point to regular processors (i.e. no groups)
		// 【中文】SortedNodeIndices 只该有 processor 节点 —— group / pruned 节点不会被加进 IndicesRemaining
		if (ensure(AllNodes[NodeIndex].Processor != nullptr))
		{
			OutResult.Add({ AllNodes[NodeIndex].Name, AllNodes[NodeIndex].Processor, FMassProcessorOrderInfo::EDependencyNodeType::Processor, DependencyNames, AllNodes[NodeIndex].SequencePositionIndex });
		}
	}
}

/**
 * 【中文】求解器主入口 —— 把 Processors 转成扁平、无冲突的执行序列。
 *
 * 完整流程（从空 AllNodes 开始）：
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │ 0) 设置 root dummy 节点（NodeIndex=0，FName 空）—— 简化空 group 处理  │
 *   │ 1) 没传 EntityManager 就 new 一个临时的（虚拟 archetype 模式）        │
 *   │ 2) GatherSubsystemInformation：拉 bThreadSafeWrite 标记的 subsystem    │
 *   │ 3) 对每个 Processor：                                                 │
 *   │    - CreateNodes 建节点 + group 树                                    │
 *   │    - bCreateVirtualArchetypes 时为它创建一个匹配它需求的虚拟 archetype │
 *   │ 4) 对每个 Processor 节点：                                            │
 *   │    - GetArchetypesMatchingOwnedQueries 算 ValidArchetypes             │
 *   │    - 没匹配到任何 archetype + 允许 query-based pruning → Processor=nullptr   │
 *   │      （prune 后该节点变成 group，仍保留 ExecuteBefore/After）         │
 *   │ 5) BuildDependencies 把 ExecuteBefore/After 转成 OriginalDependencies │
 *   │ 6) Solve 执行拓扑排序 + 资源冲突感知 → 输出 OutResult                 │
 *   │ 7) 算 MaxSequenceLength 写到 InOutOptionalResult                      │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * 虚拟 archetype 技巧（可能反直觉）：
 *   即使没传入 EntityManager，求解器也能精确知道哪些 Processor 操作不重叠的 archetype。
 *   原理：为每个 Processor 创建一个"刚好匹配它需求的最小 archetype"，然后让所有
 *   Processor 用 query 去匹配。能互相匹配的就视为操作重叠，匹配不到的就视为不重叠
 *   → 用这套虚拟 archetype 集合做 archetype-aware 冲突剪枝。
 */
void FMassProcessorDependencySolver::ResolveDependencies(TArray<FMassProcessorOrderInfo>& OutResult, TSharedPtr<FMassEntityManager> EntityManager, FMassProcessorDependencySolver::FResult* InOutOptionalResult)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass ResolveDependencies");

	if (Processors.Num() == 0)
	{
		return;
	}

	FScopedCategoryAndVerbosityOverride LogOverride(TEXT("LogMass"), ELogVerbosity::Log);

	if (InOutOptionalResult)
	{
		DependencyGraphFileName = InOutOptionalResult->DependencyGraphFileName;
	}

	UE_LOG(LogMassDependencies, Log, TEXT("Gathering dependencies data:"));

	AllNodes.Reset();
	NodeIndexMap.Reset();
	// as the very first node we add a "root" node that represents the "top level group" and also simplifies the rest
	// of the lookup code - if a processor declares it's in group None or depends on Node it we don't need to check that 
	// explicitly. 
	// 【中文】预置 root 节点 —— Processor 声明 ExecuteInGroup=None 或依赖空名时不必特判
	AllNodes.Add(FNode(FName(), nullptr, 0));
	NodeIndexMap.Add(FName(), 0);

	const bool bCreateVirtualArchetypes = (!EntityManager);
	if (bCreateVirtualArchetypes)
	{
		// create FMassEntityManager instance that we'll use to sort out processors' overlaps
		// the idea for this is that for every processor we have we create an archetype matching given processor's requirements. 
		// Once that's done we have a collection of "virtual" archetypes our processors expect. Then we ask every processor 
		// to cache the archetypes they'd accept, using processors' owned queries. The idea is that some of the nodes will 
		// end up with more than just the virtual archetype created for that specific node. The practice proved the idea correct. 
		// 【中文】没传 EntityManager → 自己建一个临时的，专门用来构造虚拟 archetype。
		//         注意这里 new 出来的是隔离的求解专用 EntityManager，不会影响游戏世界。
		EntityManager = MakeShareable(new FMassEntityManager());
	}

	GatherSubsystemInformation(EntityManager->GetTypeManager());

	// gather the processors information first
	for (UMassProcessor* Processor : Processors)
	{
		if (Processor == nullptr)
		{
			UE_LOG(LogMassDependencies, Warning, TEXT("%s nullptr found in Processors collection being processed"), ANSI_TO_TCHAR(__FUNCTION__));
			continue;
		}

		const int32 ProcessorNodeIndex = CreateNodes(*Processor);

		if (bCreateVirtualArchetypes)
		{
			// this line is a part of a nice trick we're doing here utilizing EntityManager's archetype creation based on 
			// what each processor expects, and EntityQuery's capability to cache archetypes matching its requirements (used below)
			// 【中文】关键 trick：让虚拟 EntityManager 真正创建一个该 Processor 需要的 archetype。
			//         这样下面 GetArchetypesMatchingOwnedQueries 调用时就能匹配出"哪些虚拟 archetype 能用"
			EntityManager->CreateArchetype(AllNodes[ProcessorNodeIndex].Requirements.AsCompositionDescriptor());
		}
	}

	UE_LOG(LogMassDependencies, Verbose, TEXT("Pruning processors..."));

	// 【中文】Pruning 阶段：扫所有节点，决定每个 Processor 的 ValidArchetypes，并裁掉空的
	int32 PrunedProcessorsCount = 0;
	for (FNode& Node : AllNodes)
	{
		if (Node.IsGroup() == false)
		{
			CA_ASSUME(Node.Processor); // as implied by Node.IsGroup() == false

			const bool bDoQueryBasedPruning = Node.Processor->ShouldAllowQueryBasedPruning(bGameRuntime);

			// we gather archetypes for processors that have queries OR allow query-based pruning.
			// The main point of this condition is to allow calling GetArchetypesMatchingOwnedQueries
			// on pruning-supporting processors, while having no queries - that will emmit a warning
			// that will let the user know their processor is misconfigured.
			// We do collect archetype information for the processors that never get pruned because we're
			// using this information for the dependency calculations, regardless of ShouldAllowQueryBasedPruning
			// 【中文】调用 GetArchetypesMatchingOwnedQueries 的两种动机：
			//   - 真要剪枝（bDoQueryBasedPruning）→ 必须知道有没有匹配
			//   - 不剪枝但有 query     → 仍然需要 ValidArchetypes 用于 archetype-aware 冲突检测
			//   - 既不剪枝也无 query → 跳过（避免触发"无 query"警告）
			if (bDoQueryBasedPruning || Node.Processor->GetOwnedQueriesNum())
			{
				// for each processor-representing node we cache information on which archetypes among the once we've created 
				// above (see the EntityManager.CreateArchetype call in the previous loop) match this processor. 
				Node.Processor->GetArchetypesMatchingOwnedQueries(*EntityManager.Get(), Node.ValidArchetypes);
			}

			// prune the archetype-less processors
			// 【中文】query 一个 archetype 都没匹配到 + 允许剪枝 → 当前数据下它没事可做，剪掉它
			if (Node.ValidArchetypes.Num() == 0 && bDoQueryBasedPruning)
			{
				UE_LOG(LogMassDependencies, Verbose, TEXT("\t%s"), *Node.Processor->GetName());

				if (InOutOptionalResult)
				{
					InOutOptionalResult->PrunedProcessors.Add(Node.Processor);
				}

				// clearing out the processor will result in the rest of the algorithm to treat it as a group - we still 
				// want to preserve the configured ExecuteBefore and ExecuteAfter dependencies
				// 【中文】把 Processor 设为 nullptr → IsGroup() = true → Solve 会跳过它
				//         但它的 ExecuteBefore/After 仍参与 BuildDependencies，所以"间接传递依赖"
				//         （A→prunedC→B 仍能让 A 在 B 之前）依然成立
				Node.Processor = nullptr;
				++PrunedProcessorsCount;
			}
		}
	}

	UE_LOG(LogMassDependencies, Verbose, TEXT("Number of processors pruned: %d"), PrunedProcessorsCount);

	check(AllNodes.Num());
	LogNode(AllNodes[0]);

	BuildDependencies();

	// now none of the processor nodes depend on groups - we replaced these dependencies with depending directly 
	// on individual processors. However, we keep the group nodes around since we store the dependencies via index, so 
	// removing nodes would mess that up. Solve below ignores group nodes and OutResult will not have any groups once its done.
	// 【中文】BuildDependencies 后所有依赖都已下放到 processor 级，但我们不删 group 节点
	//         —— 因为依赖用 NodeIndex 存储，删节点会破坏所有索引。Solve 内部跳过 group 即可。

	Solve(OutResult);

	UE_LOG(LogMassDependencies, Verbose, TEXT("Dependency order:"));
	for (const FMassProcessorOrderInfo& Info : OutResult)
	{
		UE_LOG(LogMassDependencies, Verbose, TEXT("\t%s"), *Info.Name.ToString());
	}

	// 【中文】统计最长依赖链 —— ParallelDispatch 据此分配多少个 fence/event
	int32 MaxSequenceLength = 0;
	for (FNode& Node : AllNodes)
	{
		MaxSequenceLength = FMath::Max(MaxSequenceLength, Node.SequencePositionIndex);
	}

	UE_LOG(LogMassDependencies, Verbose, TEXT("Max sequence length: %d"), MaxSequenceLength);

	if (InOutOptionalResult)
	{
		InOutOptionalResult->MaxSequenceLength = MaxSequenceLength;
		// 【中文】记录此次求解时的 archetype 版本 → 后续 IsResultUpToDate 用它判断缓存是否失效
		InOutOptionalResult->ArchetypeDataVersion = EntityManager->GetArchetypeDataVersion();
	}
}

/**
 * 【中文】缓存有效性检查。
 *
 * 上次求解的 InResult 现在还能用吗？三步判断（任一为真即"还能用"）：
 *   1) 上次没剪枝过任何 Processor → 求解结果与 archetype 集合无关 → 永远有效
 *   2) 没传 EntityManager → 不知道当前状态 → 假定有效（兜底，避免不必要的重算）
 *   3) ArchetypeDataVersion 一致 → 引擎自上次求解后没新增/删除 archetype → 有效
 *
 * 否则就需要逐个检查被剪枝的 Processor：现在它们能匹配到 archetype 了吗？
 *   - 有任何一个能匹配 → 它本应执行但被错误地剪掉 → 必须重新求解（返回 false）
 *   - 全都仍匹配不到   → 缓存仍然有效
 *
 * 这是 Mass 调度系统"运行时近乎零成本"的关键 —— 大多数帧 ArchetypeDataVersion 都没变。
 *
 * @return true 表示 InResult 仍可直接使用；false 表示必须重新调 ResolveDependencies
 */
bool FMassProcessorDependencySolver::IsResultUpToDate(const FMassProcessorDependencySolver::FResult& InResult, TSharedPtr<FMassEntityManager> EntityManager)
{
	if (InResult.PrunedProcessors.Num() == 0 
		|| !EntityManager 
		|| InResult.ArchetypeDataVersion == EntityManager->GetArchetypeDataVersion())
	{
		return true;
	}

	// Would be more efficient if we had a common place where all processors live, both active and inactive, so that we can utilize those.
	// 【中文】@todo Epic 注释：理想中应该有个全局 Processor 注册表，剪掉的也能在里面找到 —— 现在
	//         只能逐个检查 PrunedProcessors，未剪枝的那部分如果 archetype 集合变了，不会被重新评估
	//         （潜在问题见总结）
	for (UMassProcessor* PrunedProcessor : InResult.PrunedProcessors)
	{
		if (PrunedProcessor && PrunedProcessor->DoesAnyArchetypeMatchOwnedQueries(*EntityManager.Get()))
		{
			return false;
		}
	}
	return true;
}

/**
 * 【中文】扫一遍 TypeManager 的所有 Subsystem，记录哪些是"线程安全可并写"的。
 *
 * bThreadSafeWrite 的语义：这个 subsystem 的实现保证了多线程同时写也不会出问题
 * （比如内部用了原子操作或 lock-free 数据结构）→ 调度器无需为它的访问生成依赖边
 * → 在 CreateNodes 中从 Required.Subsystems.Read/Write 中减掉 → 这些 subsystem
 * 对求解器"不存在"。
 *
 * @注意：这是一个性能优化点。Subsystem 默认是非线程安全的（保守策略），用户必须
 *        显式声明 bThreadSafeWrite=true 才能享受这个优化。
 */
void FMassProcessorDependencySolver::GatherSubsystemInformation(const UE::Mass::FTypeManager& TypeManager)
{
	using namespace UE::Mass;

	if (TypeManager.IsEmpty())
	{
		return;
	}

	for (FTypeManager::FSubsystemTypeConstIterator SubsystemTypeIterator = TypeManager.MakeSubsystemIterator(); SubsystemTypeIterator; ++SubsystemTypeIterator)
	{
		if (const FTypeInfo* TypeInfo = TypeManager.GetTypeInfo(*SubsystemTypeIterator))
		{
			const FSubsystemTypeTraits* SubsystemTraits = TypeInfo->GetAsSystemTraits();
			check(SubsystemTraits);
			if (SubsystemTraits->bThreadSafeWrite)
			{
				const UClass* SubsystemClass = SubsystemTypeIterator->GetClass();
				check(SubsystemClass);
				MultiThreadedSystemsBitSet.Add(*SubsystemClass);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE