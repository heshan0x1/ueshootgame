// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassExternalSubsystemTraits.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_6
#include "MassEntityQuery.h"
#include "MassArchetypeTypes.h"
#include "MassSubsystemAccess.h"
#include "MassProcessor.h"


// 校验：要访问的 fragment 必须已绑定（即 query 声明过且当前 chunk 中已 ApplyFragmentRequirements）。
// 如果未声明就调 GetXxxFragment，会触发 checkf 报错。
#define CHECK_IF_VALID(View, Type) \
	checkf(View \
		, TEXT("Requested fragment type not bound, type %s. Make sure it has been listed as required."), *GetNameSafe(Type))

// 校验：要写的 fragment 在 query 声明中必须是 ReadWrite。
// 防止误把 ReadOnly fragment 当作 Mutable 来改。
#define CHECK_IF_READWRITE(View) \
	checkf(View == nullptr || View->Requirement.AccessMode == EMassFragmentAccess::ReadWrite \
		, TEXT("Requested fragment type not bound for writing, type %s. Make sure it has been listed as required in ReadWrite mode.") \
		, View ? *GetNameSafe(View->Requirement.StructType) : TEXT("[Not found]"))

struct FMassEntityQuery;

/**
 * FMassExecutionContext —— processor 回调中"当前 chunk 的视图对象"。
 *
 * 角色定位：
 *  - 在 query.ForEachEntityChunk(Context, Lambda) 期间，每个 archetype 的每个 chunk 都会
 *    把自己当前的 fragment 列、entity 列、shared/chunk fragment 等"绑"到 Context 上，
 *    Lambda 通过 Context 提供的类型化访问器读写它们。
 *  - Context 是 chunk 局部对象，**生命周期不可跨 chunk 复用**：
 *    当 query 执行切到下一个 chunk 时会先 ClearFragmentViews 再重新 ApplyFragmentRequirements。
 *  - 暴露给用户的 fragment view 都是非拥有的 TArrayView/FStructView，
 *    **processor 不能持有它们**（指针指向 chunk 内部内存，chunk 复用/移动后即失效）。
 *
 * 内部布局：
 *  - 4 种 fragment 视图容器（FragmentViews / ChunkFragmentViews / ConstSharedFragmentViews / SharedFragmentViews）
 *    都是 `TArray<TFragmentView<XXX>>`，按 query 的 requirement 列表填充。
 *  - 所有 GetXxxFragmentView<T>() 都用 `FindByPredicate(StructType==T)` 在对应容器中线性查找。
 *    由于 query 通常只声明少量 fragment（个位数），线性查找比维护额外 hash 更划算
 *    （而且 InlineAllocator 让数据贴热缓存）。
 *  - 真正的位集索引(FMassFragmentBitSet 的位)只在 query 与 archetype 匹配时使用；
 *    Context 自己只看 StructType 指针。
 *
 * 嵌套查询（query 栈）：
 *  - 一个 query 的 lambda 内部允许再调一个 query；为此 Context 维护 QueriesStack：
 *    - PushQuery：保存当前的 ConstSubsystems/MutableSubsystems BitSet 现场（FQueryTransientRuntime）。
 *    - PopQuery：恢复保存的现场，从而内层 query 不会污染外层的 subsystem 访问权限。
 */
struct FMassExecutionContext
{
private:

	/**
	 * 单条 fragment requirement 的运行期映射条目。
	 * - Requirement：来自 query 的描述（StructType + AccessMode）；
	 * - FragmentView：当 chunk "绑定"到 Context 后，指向该 chunk 内对应列的视图（TArrayView 或 FStructView）。
	 * 模板特化：ViewType 不同（TArrayView<FMassFragment> 用于 per-entity；FStructView 用于单实例的 chunk/shared fragment）。
	 */
	template< typename ViewType >
	struct TFragmentView 
	{
		FMassFragmentRequirementDescription Requirement;
		ViewType FragmentView;

		TFragmentView() {}
		explicit TFragmentView(const FMassFragmentRequirementDescription& InRequirement) : Requirement(InRequirement) {}

		/** 便利比较：让 FindByPredicate 可直接用 UScriptStruct* 作为查询 key。 */
		bool operator==(const UScriptStruct* FragmentType) const { return Requirement.StructType == FragmentType; }
	};
	/** 普通 fragment（每实体一份）：视图为 chunk 内一段连续 FMassFragment 数组。 */
	using FFragmentView = TFragmentView<TArrayView<FMassFragment>>;
	/** InlineAllocator<8>：典型 query 对 fragment 的需求数 ≤ 8，避免堆分配。 */
	TArray<FFragmentView, TInlineAllocator<8>> FragmentViews;

	/** Chunk fragment（每 chunk 一份单实例）：视图为指向 chunk 头部的单 FStructView。 */
	using FChunkFragmentView = TFragmentView<FStructView>;
	TArray<FChunkFragmentView, TInlineAllocator<4>> ChunkFragmentViews;

	/** 只读 shared fragment：跨多个 archetype 共享的同一份只读结构（如配置），FConstStructView 表示。 */
	using FConstSharedFragmentView = TFragmentView<FConstStructView>;
	TArray<FConstSharedFragmentView, TInlineAllocator<4>> ConstSharedFragmentViews;

	/** 可写 shared fragment：共享但允许 Mutable 访问，FStructView 表示。 */
	using FSharedFragmentView = TFragmentView<FStructView>;
	TArray<FSharedFragmentView, TInlineAllocator<4>> SharedFragmentViews;

	/** 5 类 USubsystem 派生的缓存访问器，详见 FMassSubsystemAccess 类注释。 */
	FMassSubsystemAccess SubsystemAccess;
	
	// mz@todo make this shared ptr thread-safe and never auto-flush in MT environment. 
	// 延迟命令缓冲：lambda 中"延后到 query 结束才执行"的结构性改动（销毁/添加 fragment 等）会先入此 buffer，
	// 由 FlushDeferred 一次性提交。共享指针使其能在父子 Context 间被引用。
	TSharedPtr<FMassCommandBuffer> DeferredCommandBuffer;
	/** 当前 chunk 的 entity handle 列表视图（指向 chunk 内 EntitiesArray）。chunk 切换后失效。 */
	TArrayView<FMassEntityHandle> EntityListView;
	
	/** If set this indicates the exact archetype and its chunks to be processed. 
	 *  @todo this data should live somewhere else, preferably be just a parameter to Query.ForEachEntityChunk function */
	/**
	 * 若设置，则限定 query 仅遍历指定 archetype 的指定 chunk 集合（稀疏 chunk 模式）。
	 * 用于"只对一组特定 entity 执行 query"的场景，避免遍历无关 entity。
	 */
	FMassArchetypeEntityCollection EntityCollection;
	
	/** @todo rename to "payload" */
	/** 调用方传给 query 的"附加载荷"，类型由 InstancedStruct 自描述，processor 通过 ValidateAuxDataType 校验。 */
	FInstancedStruct AuxData;
	/** 当前帧 deltaSeconds，processor lambda 常用作积分步长。 */
	float DeltaTimeSeconds = 0.0f;
	/** 当前 chunk 的"修改序号"，用于 chunk 级失效检测；-1 表示尚未绑定到具体 chunk。 */
	int32 ChunkSerialModificationNumber = -1;
	/** 当前 chunk 所属 archetype 的完整组成描述（fragment/tag 集合）。供 DoesArchetypeHaveFragment/Tag 查询。 */
	FMassArchetypeCompositionDescriptor CurrentArchetypeCompositionDescriptor;
#if WITH_MASSENTITY_DEBUG
	/** Debug 着色：MassDebugger 在 visualizer 中按 archetype 分色显示 entity。 */
	FColor DebugColor;
#endif // WITH_MASSENTITY_DEBUG

	/** 持有 EntityManager 的强引用，确保 Context 生命期内 EntityManager 不被释放。 */
	TSharedRef<FMassEntityManager> EntityManager;

	/**
	 * 单个 query 的"现场快照"。
	 * 嵌套 query 时入栈以保存外层状态：
	 *  - Query：当前正在执行的 query 指针（用于 Pop 时一致性校验，以及 lambda 中通过 GetCurrentQuery() 反查）。
	 *  - ConstSubsystemsBitSet/MutableSubsystemsBitSet：进入此 query 之前 SubsystemAccess 的位掩码现场，
	 *    Pop 时恢复，避免内层 query 改完位掩码后影响外层的访问校验。
	 *  - Debug 字段：MassDebugger 的断点（基于 fragment 写入或 processor 行为）的命中状态缓存，
	 *    在 PushQuery 时计算一次以避免每次 ++iterator 都做昂贵检查。
	 *  - IteratorSerialNumber：每次 PushQuery 自增；FEntityIterator 持有它的副本，
	 *    保证"再次创建迭代器"得到一个不等价的实例（用于诊断 chunk 切换时的迭代器复用错误）。
	 */
	struct FQueryTransientRuntime
	{
		TNotNull<FMassEntityQuery*> Query;
		FMassExternalSubsystemBitSet ConstSubsystemsBitSet;
		FMassExternalSubsystemBitSet MutableSubsystemsBitSet;
#if WITH_MASSENTITY_DEBUG
		/** MaxBreakFragmentCount needs to be bigger than the greatest number of fragments a query has a write requirement for to that can have a breakpoint set */
		/** 单个 query 同时可命中的 fragment-write 断点上限；TStaticArray 避免动态分配。 */
		static constexpr uint32 MaxFragmentBreakpointCount = 8;
		TStaticArray<const UScriptStruct*, MaxFragmentBreakpointCount> FragmentTypesToBreakOn;

		/** 是否需要为本 query 检查 processor 级断点。 */
		bool bCheckProcessorBreaks = false;
		/** FragmentTypesToBreakOn 中实际填充的元素数。 */
		int32 BreakFragmentsCount = 0;
#endif // WITH_MASSENTITY_DEBUG

		/** Serial number to ensure iterator consistency (subsequent calls to CreateEntityIterator should not pass equivelncy test) */
		/** 用于 FEntityIterator 的一致性校验序号；详见类注释。 */
		uint32 IteratorSerialNumber = 0;

		/** Helper function to create an empty instance with a valid Query ptr */
		/** 提供一个全局 dummy 实例（带合法 Query 指针）—— 用于 FEntityIterator 默认构造的安全占位。 */
		static FQueryTransientRuntime& GetDummyInstance();
	};

	/** We usually expect the queries to go only a single layer deep, so 2 elements here should suffice most of the time */
	/** Query 栈：嵌套 query 时入栈外层、内层执行完出栈。InlineAllocator<2> 因绝大多数情况只嵌套 1 层。 */
	TArray<FQueryTransientRuntime, TInlineAllocator<2>> QueriesStack;

	/** Track the serial number for FEntityIterator creation */
	/** 单调递增的迭代器序号生成器；每次 PushQuery 取下一个值赋给 FQueryTransientRuntime::IteratorSerialNumber。 */
	uint32 IteratorSerialNumberGenerator = 0;

#if WITH_MASSENTITY_DEBUG
	/** 调试用执行描述字符串（如 "PostPhysics phase: AvoidanceProcessor"），由 MassDebugger 显示。 */
	FString DebugExecutionDescription;

	/** Currently executing processor, used for debugger breakpoint checking */
	// js@todo make this more generic
	/** 当前正在执行的 processor 弱指针；MassDebugger 用以判断"当前处于哪个 processor 内"以匹配断点。 */
	TWeakObjectPtr<UMassProcessor> DebugProcessor;
#endif // WITH_MASSENTITY_DEBUG
	
	/** Used to control when the context is allowed to flush commands collected in DeferredCommandBuffer. This mechanism 
	 * is mainly utilized to avoid numerous small flushes in favor of fewer larger ones. */
	/**
	 * 是否允许在合适时机自动 flush DeferredCommandBuffer。
	 * 通常一组连续的 query 都共享同一个命令缓冲，关此 flag 可以攒一波再统一 flush，减少散碎提交开销。
	 */
	bool bFlushDeferredCommands = true;

	/** 内部访问：返回 4 种 fragment view 容器的可变视图。仅供框架/Archetype 内部代码使用。 */
	TArrayView<FFragmentView> GetMutableRequirements() { return FragmentViews; }
	TArrayView<FChunkFragmentView> GetMutableChunkRequirements() { return ChunkFragmentViews; }
	TArrayView<FConstSharedFragmentView> GetMutableConstSharedRequirements() { return ConstSharedFragmentViews; }
	TArrayView<FSharedFragmentView> GetMutableSharedRequirements() { return SharedFragmentViews; }

	/** 转发到 SubsystemAccess：取 Const/Mutable 位掩码副本（PushQuery 用以保存现场）。 */
	void GetSubsystemRequirementBits(FMassExternalSubsystemBitSet& OutConstSubsystemsBitSet, FMassExternalSubsystemBitSet& OutMutableSubsystemsBitSet)
	{
		SubsystemAccess.GetSubsystemRequirementBits(OutConstSubsystemsBitSet, OutMutableSubsystemsBitSet);
	}

	/** 转发到 SubsystemAccess：覆盖位掩码（PopQuery 用以恢复现场）。 */
	void SetSubsystemRequirementBits(const FMassExternalSubsystemBitSet& InConstSubsystemsBitSet, const FMassExternalSubsystemBitSet& InMutableSubsystemsBitSet)
	{
		SubsystemAccess.SetSubsystemRequirementBits(InConstSubsystemsBitSet, InMutableSubsystemsBitSet);
	}

	/** 当前执行类型（Local / Processor 等），影响是否允许自动 flush 等行为。 */
	EMassExecutionContextType ExecutionType = EMassExecutionContextType::Local;

	/** Archetype 内部代码持有 Context 的私有引用，用以在迭代过程中向 Context 写入 chunk 视图等数据。 */
	friend FMassArchetypeData;

	/**
	 * Note that this operator is private on purpose, used to simplify the implementation of constructors.
	 * The context itself does not support assignment.
	 */
	/**
	 * 注意：赋值运算符故意设为 private、仅用作拷贝构造内部辅助。
	 * Context 不允许业务代码进行赋值（语义上"两个 Context 共享同一个 chunk 视图"是危险的）。
	 */
	FMassExecutionContext& operator=(const FMassExecutionContext& Other) = default;

public:
	/**
	 * 主构造：从 EntityManager 派生 Context。
	 * @param InDeltaTimeSeconds  当前帧 dt，processor lambda 通过 GetDeltaTimeSeconds 读取。
	 * @param bInFlushDeferredCommands 是否允许自动 flush DeferredCommandBuffer。
	 * @note SubsystemAccess 用 EntityManager 关联的 World 初始化。
	 */
	MASSENTITY_API explicit FMassExecutionContext(FMassEntityManager& InEntityManager, const float InDeltaTimeSeconds = 0.f, const bool bInFlushDeferredCommands = true);
	/** 移动构造：默认即可（成员都支持移动）。 */
	FMassExecutionContext(FMassExecutionContext&& Other) = default;
	/** 拷贝构造：复制 fragment view 与设置，但 QueriesStack 在拷贝后会被清空（子 context 不继承父的 query 栈）。 */
	MASSENTITY_API FMassExecutionContext(const FMassExecutionContext& Other);
	/**
	 * 派生构造：从父 Context 派生一个"只关心单个 query"的子 Context。
	 * 用于 ParallelForEach 等需要每条线程独立 Context 但共享父配置的场景。
	 * @param Query 子 Context 唯一关心的 query；必须等于 Other 当前栈顶 query（构造内 ensure 校验）。
	 * @param InCommandBuffer 子 Context 自己的命令缓冲（典型并行场景每线程独立一个，最后并入父的）。
	 */
	MASSENTITY_API FMassExecutionContext(const FMassExecutionContext& Other, FMassEntityQuery& Query, const TSharedPtr<FMassCommandBuffer>& InCommandBuffer = {});
	/** 析构：会 ensure 当前 QueriesStack 为空，否则提示"query push/pop 不配对"。 */
	MASSENTITY_API ~FMassExecutionContext();

	/** For internal use only, should never be exported as part of API */
	/** 仅内部使用：返回一个 dummy 单例（用于 FEntityIterator 默认构造时的安全占位）。 */
	MASSENTITY_API static FMassExecutionContext& GetDummyInstance();

	/** 取关联的 EntityManager（若 Context 已构造，引用必合法）。 */
	FMassEntityManager& GetEntityManagerChecked() const;
	/** 取共享引用，便于把 EntityManager 跨调用边界传递。 */
	const TSharedRef<FMassEntityManager>& GetSharedEntityManager();

#if WITH_MASSENTITY_DEBUG
	/** 取 / 设置当前 Context 的调试描述（MassDebugger 显示用）。 */
	const FString& DebugGetExecutionDesc() const { return DebugExecutionDescription; }
	void DebugSetExecutionDesc(const FString& Description) { DebugExecutionDescription = Description; }
	void DebugSetExecutionDesc(FString&& Description) { DebugExecutionDescription = MoveTemp(Description); }

	/** 取 / 设置当前正在执行的 processor，用于断点匹配。 */
	UMassProcessor* DebugGetProcessor() const { return DebugProcessor.Get(); }
	void DebugSetProcessor(UMassProcessor* Processor) { DebugProcessor = Processor; }
#endif

	//-----------------------------------------------------------------------------
	// Query 栈管理
	//-----------------------------------------------------------------------------
	/**
	 * 将一个 query 入栈：
	 *  - 保存进入前的 SubsystemAccess 位掩码现场到栈顶 FQueryTransientRuntime；
	 *  - 在 Debug 构建中预计算 processor/fragment 断点表，避免迭代时反复检查；
	 *  - 分配一个新的 IteratorSerialNumber。
	 * 调用时机：每个 FMassEntityQuery::ForEachChunk 等执行入口在开始时调用。
	 */
	void PushQuery(FMassEntityQuery& InQuery);
	/**
	 * 将栈顶 query 出栈：
	 *  - 校验 InQuery == 栈顶 Query（LIFO 一致性）；
	 *  - 用栈顶保存的位掩码恢复 SubsystemAccess 现场。
	 */
	void PopQuery(const FMassEntityQuery& InQuery);
	/** 取当前栈顶 query 的引用；要求栈非空（check）。供 lambda 反查"我现在在哪条 query 中"。 */
	const FMassEntityQuery& GetCurrentQuery() const;
	/** 测试某 query 是否就是当前栈顶。 */
	bool IsCurrentQuery(const FMassEntityQuery& Query) const;
	/**
	 * 在 chunk 切换时调用：根据 RequestingQuery 的 Requirement 列表把 4 类 FragmentView 容器
	 * 重置并填充 Requirement 描述（FragmentView 自身置空，待 archetype 代码后续 bind 进 chunk 视图）。
	 * 调用约束：仅当 RequestingQuery 是当前栈顶 query 时才允许（check）。
	 */
	void ApplyFragmentRequirements(const FMassEntityQuery& RequestingQuery);
	/**
	 * 在 chunk 结束 / query 切换时调用：把所有 FragmentView 重置为空 view，
	 * 防止旧 chunk 的视图泄漏到下一个 chunk 的 lambda 中。
	 * 调用约束：仅栈顶 query 可调用。
	 */
	void ClearFragmentViews(const FMassEntityQuery& RequestingQuery);

	/**
	 * Iterator to easily loop through entities in the current chunk.
	 * Supports ranged for and can be used directly as an entity index for the current chunk.
	 */
	/**
	 * FEntityIterator —— 当前 chunk 内"实体下标"的 range-based-for 迭代器。
	 *
	 * 用法：
	 *   for (FMassExecutionContext::FEntityIterator It = Context.CreateEntityIterator(); It; ++It)
	 *   {
	 *       const int32 EntityIndex = It;                  // 隐式转 int32
	 *       FMassEntityHandle Handle = Context.GetEntity(It);
	 *       ...
	 *   }
	 *
	 * 关键点：
	 *  - 迭代器返回的是 chunk 内"行索引" int32（隐式 operator int32），不是 FMassEntityHandle；
	 *    需要 handle 时通过 Context.GetEntity(It) 取，避免在 hot loop 中拷贝 8 字节 handle。
	 *  - 复制构造/赋值被禁用：Iterator 持有 chunk 视图的隐含约定，复制语义复杂且当前未需要，
	 *    若未来出现合法用例再考虑放开。
	 *  - SerialNumber 与 QueryTransientRuntime 的 IteratorSerialNumber 配对：
	 *    可用于在 debug 中诊断"跨 chunk/跨 query 复用旧迭代器"的错误（当前实现未做强制校验）。
	 */
	struct FEntityIterator
	{
		/** 取当前 entity 在 chunk 内的下标。 */
		inline int32 operator*() const
		{
			return EntityIndex;
		}

		/** 与 end 哨兵比较：兼容 range-based for 的 begin/end 协议。 */
		inline bool operator!=(const int& Other) const
		{
			return EntityIndex != Other;
		}

		/** 隐式转 int32：让 Iterator 可直接作为 entity 行下标传给 Context.GetEntity 等 API。 */
		inline operator int32() const
		{
			return EntityIndex;
		}

		/** 隐式转 bool：迭代器是否仍指向有效行（未越界）。SerialNumber==0 表示 dummy/默认构造。 */
		inline operator bool() const
		{
			return SerialNumber && EntityIndex < NumEntities;
		}

		/** 与 int 大小比较：辅助 dummy 默认构造（SerialNumber==0）下 < 永远为 false 的语义。 */
		inline bool operator<(const int32 Other) const
		{
			return SerialNumber && EntityIndex != INDEX_NONE && EntityIndex < Other;
		}

		/**
		 * 前缀 ++：移到下一行；如启用 Debug 断点检查则在每步触发 TestBreakpoints。
		 * 注意：通过 UNLIKELY 把断点检查路径置冷，正常运行不付出代价。
		 */
		inline FEntityIterator& operator++()
		{
			++EntityIndex;
#if WITH_MASSENTITY_DEBUG
			if (UNLIKELY(QueryRuntime.bCheckProcessorBreaks || QueryRuntime.BreakFragmentsCount != 0) 
				&& EntityIndex < NumEntities)
			{
				TestBreakpoints();
			}
#endif //WITH_MASSENTITY_DEBUG
			return *this;
		}

		/** 后缀 ++：仅复用前缀 ++，不返回旧值（避免拷贝；与 deleted copy ctor 一致）。 */
		inline void operator++(int)
		{
			++(*this);
		}

		/** range-based for 协议：begin 即自身（move 语义）。 */
		FEntityIterator&& begin()
		{
			return MoveTemp(*this);
		}

		/** range-based for 协议：end 是一个仅用于比较的"哨兵"，EntityIndex==NumEntities。 */
		FEntityIterator end() const
		{
			FEntityIterator End;
			End.EntityIndex = NumEntities;
			return End;
		}

		/** 默认构造：得到 dummy 占位（指向 dummy ExecutionContext / dummy QueryRuntime），operator bool 永远 false。 */
		MASSENTITY_API FEntityIterator();
		/** 移动构造：默认即可。 */
		FEntityIterator(FEntityIterator&&) = default;

		FEntityIterator& operator=(const FEntityIterator&) = delete;
		FEntityIterator& operator=(FEntityIterator&&) = delete;
		/**
		 * Iterator copying is disabled to avoid additional checks to detect if entity chunk being iterated on changed.
		 * This decision is to be reconsidered when valid iterator-copying scenarios emerge. 
		 */
		FEntityIterator(const FEntityIterator&) = delete;

	private:
		friend FMassExecutionContext;
		/** 仅给 ExecutionContext 调用：构造一个有 ExecutionContext 但 QueryRuntime 为 dummy 的迭代器（容错路径）。 */
		FEntityIterator(FMassExecutionContext& InExecutionContext);
		/** 正式入口：CreateEntityIterator 在栈非空时通过此构造产生有效迭代器。 */
		FEntityIterator(FMassExecutionContext& InExecutionContext, FQueryTransientRuntime& InQueryRuntime);

#if WITH_MASSENTITY_DEBUG
		/** 在迭代时检查 processor/fragment-write 级断点；命中则触发 UE_DEBUG_BREAK。 */
		MASSENTITY_API void TestBreakpoints();
#endif //!WITH_MASSENTITY_DEBUG

		/** 持有引用（不拥有）：调用方需保证迭代器在 Context 之内消亡。 */
		const FMassExecutionContext& ExecutionContext;
		const FQueryTransientRuntime& QueryRuntime;
		/** 当前 entity 在 chunk 内的行号；INDEX_NONE 即未开始（构造内会 ++ 一次到 0）。 */
		int32 EntityIndex = INDEX_NONE;
		/** 当前 chunk 的实体总数；range 区间为 [0, NumEntities)。 */
		const int32 NumEntities = INDEX_NONE;
		/** 创建时 QueryRuntime 的 IteratorSerialNumber 副本；==0 表示 dummy/默认构造。 */
		const uint32 SerialNumber = 0;
	};

	/**
	 * Creates an Entity Iterator for the current chunk.
	 * Supports range-based for loop and can be used directly as an entity index for the current chunk.
	 */
	/**
	 * 创建当前 chunk 的实体迭代器。
	 * 调用约束：必须在某个 query 执行期间（QueriesStack 非空）；否则会 testableEnsureMsgf 报错并返回容错的 dummy 迭代器。
	 */
	MASSENTITY_API FEntityIterator CreateEntityIterator();

	/** Sets bFlushDeferredCommands. Note that setting to True while the system is being executed doesn't result in
	 *  immediate commands flushing */
	/** 设置 bFlushDeferredCommands。注意：在执行中改成 true 不会立刻 flush，仅影响下一次 FlushDeferred 行为。 */
	void SetFlushDeferredCommands(const bool bNewFlushDeferredCommands);
	/** 替换 DeferredCommandBuffer。常用于子 Context 构造时把父 buffer 替换为子线程独立 buffer。 */
	void SetDeferredCommandBuffer(const TSharedPtr<FMassCommandBuffer>& InDeferredCommandBuffer);
	/** 设定/移动稀疏 chunk 集合。要求当前未设置（防止覆盖错误）；移动版另校验 InEntityCollection 是 up-to-date。 */
	MASSENTITY_API void SetEntityCollection(const FMassArchetypeEntityCollection& InEntityCollection);
	MASSENTITY_API void SetEntityCollection(FMassArchetypeEntityCollection&& InEntityCollection);
	/** 重置稀疏 chunk 集合。下一次 query 将不再受其限制。 */
	void ClearEntityCollection();
	/** 设置 AuxData 载荷。 */
	void SetAuxData(const FInstancedStruct& InAuxData);

	/** 设置/取得执行类型，影响 flush 等策略。 */
	void SetExecutionType(EMassExecutionContextType InExecutionType);
	EMassExecutionContextType GetExecutionType() const;

	/** 取当前帧 dt（构造时传入）。 */
	float GetDeltaTimeSeconds() const;

	/** 取关联的 UWorld（来自 EntityManager）。可能为 nullptr（编辑器/无 World 场景）。 */
	MASSENTITY_API UWorld* GetWorld();

	/** 取共享指针形式的延迟命令缓冲，便于跨调用传递。 */
	TSharedPtr<FMassCommandBuffer> GetSharedDeferredCommandBuffer() const { return DeferredCommandBuffer; }
	/** 直接拿到延迟命令缓冲引用，用于 lambda 中提交结构性修改：Context.Defer().DestroyEntity(...) 等。 */
	FMassCommandBuffer& Defer() const { checkSlow(DeferredCommandBuffer.IsValid()); return *DeferredCommandBuffer.Get(); }

	//-----------------------------------------------------------------------------
	// Entity 访问
	//-----------------------------------------------------------------------------
	/** 当前 chunk 内 entity handle 的只读视图。范围对应 [0, GetNumEntities())。 */
	TConstArrayView<FMassEntityHandle> GetEntities() const { return EntityListView; }
	/** 当前 chunk 内的 entity 数。 */
	int32 GetNumEntities() const { return EntityListView.Num(); }

	/** 按行下标取 entity handle。 */
	FMassEntityHandle GetEntity(const int32 Index) const
	{
		return EntityListView[Index];
	}

	/**
	 * 在当前 chunk 内对每个 entity 调用一次回调函数。
	 * 等价于 for (FEntityIterator It = CreateEntityIterator(); It; ++It) { Func(*this, It); }。
	 */
	void ForEachEntityInChunk(const FMassEntityExecuteFunction& EntityExecuteFunction)
	{
		for (FEntityIterator EntityIterator = CreateEntityIterator(); EntityIterator; ++EntityIterator)
		{
			EntityExecuteFunction(*this, EntityIterator);
		}
	}

	//-----------------------------------------------------------------------------
	// Archetype 组成查询（不依赖于 query 是否声明该 fragment/tag）
	//-----------------------------------------------------------------------------
	/** 当前 chunk 所属 archetype 是否包含某 fragment 类型。可在 lambda 中用来动态分支。 */
	bool DoesArchetypeHaveFragment(const UScriptStruct& FragmentType) const
	{
		return CurrentArchetypeCompositionDescriptor.GetFragments().Contains(FragmentType);
	}

	/** 模板版本，编译期把 T 转成 UScriptStruct*。要求 T 是合法 fragment 类型（concept 校验）。 */
	template<typename T>
	bool DoesArchetypeHaveFragment() const
	{
		static_assert(UE::Mass::CFragment<T>, MASS_INVALID_FRAGMENT_MSG);
		return CurrentArchetypeCompositionDescriptor.GetFragments().Contains<T>();
	}

	/** 查询 archetype 是否带某 tag 类型。 */
	bool DoesArchetypeHaveTag(const UScriptStruct& TagType) const
	{
		return CurrentArchetypeCompositionDescriptor.GetTags().Contains(TagType);
	}

	/** 模板版本，T 必须为合法 tag 类型。 */
	template<typename T>
	bool DoesArchetypeHaveTag() const
	{
		static_assert(UE::Mass::CTag<T>, "Given struct is not of a valid tag type.");
		return CurrentArchetypeCompositionDescriptor.GetTags().Contains<T>();
	}

#if WITH_MASSENTITY_DEBUG
	/** 取调试着色（按 archetype 区分）。 */
	FColor DebugGetArchetypeColor() const
	{
		return DebugColor;
	}
#endif // WITH_MASSENTITY_DEBUG

	/** Chunk related operations */
	//-----------------------------------------------------------------------------
	// Chunk 元数据
	//-----------------------------------------------------------------------------
	/** 由 archetype 内部代码在切到新 chunk 时设置的"chunk 修改序号"，可供失效检测。 */
	void SetCurrentChunkSerialModificationNumber(const int32 SerialModificationNumber) { ChunkSerialModificationNumber = SerialModificationNumber; }
	int32 GetChunkSerialModificationNumber() const { return ChunkSerialModificationNumber; }

	//-----------------------------------------------------------------------------
	// Chunk fragment 访问（每 chunk 单实例）
	//-----------------------------------------------------------------------------
	/**
	 * 获取当前 chunk 的可写 chunk fragment 指针。
	 * - 在 ChunkFragmentViews 中按 StructType 线性查找；
	 * - CHECK_IF_READWRITE 校验 query 声明为 ReadWrite 才允许写。
	 * - 未声明则返回 nullptr。
	 */
	template<typename T>
	T* GetMutableChunkFragmentPtr()
	{
		static_assert(UE::Mass::CChunkFragment<T>, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");

		const UScriptStruct* Type = T::StaticStruct();
		FChunkFragmentView* FoundChunkFragmentData = ChunkFragmentViews.FindByPredicate([Type](const FChunkFragmentView& Element) { return Element.Requirement.StructType == Type; } );
		CHECK_IF_READWRITE(FoundChunkFragmentData);
		return FoundChunkFragmentData ? FoundChunkFragmentData->FragmentView.GetPtr<T>() : static_cast<T*>(nullptr);
	}
	
	/** 同上但返回引用；找不到会 check 崩。 */
	template<typename T>
	T& GetMutableChunkFragment()
	{
		T* ChunkFragment = GetMutableChunkFragmentPtr<T>();
		CHECK_IF_VALID(ChunkFragment, T::StaticStruct());
		return *ChunkFragment;
	}

	/** 只读版指针。允许 ReadOnly/ReadWrite 任一访问声明。 */
	template<typename T>
	const T* GetChunkFragmentPtr() const
	{
		static_assert(UE::Mass::CChunkFragment<T>, "Given struct doesn't represent a valid chunk fragment type. Make sure to inherit from FMassChunkFragment or one of its child-types.");

		const UScriptStruct* Type = T::StaticStruct();
		const FChunkFragmentView* FoundChunkFragmentData = ChunkFragmentViews.FindByPredicate([Type](const FChunkFragmentView& Element) { return Element.Requirement.StructType == Type; } );
		return FoundChunkFragmentData ? FoundChunkFragmentData->FragmentView.GetPtr<T>() : static_cast<const T*>(nullptr);
	}
	
	/** 只读版引用；找不到会 check 崩。 */
	template<typename T>
	const T& GetChunkFragment() const
	{
		const T* ChunkFragment = GetChunkFragmentPtr<T>();
		CHECK_IF_VALID(ChunkFragment, T::StaticStruct());
		return *ChunkFragment;
	}

	/** Shared fragment related operations */
	//-----------------------------------------------------------------------------
	// Shared fragment 访问（多 archetype 共享单实例）
	//-----------------------------------------------------------------------------
	/** 通过 UScriptStruct* 取只读 shared fragment 的原始内存指针；用于反射式访问。 */
	const void* GetConstSharedFragmentPtr(const UScriptStruct& SharedFragmentType) const
	{
		const FConstSharedFragmentView* FoundSharedFragmentData = ConstSharedFragmentViews.FindByPredicate([&SharedFragmentType](const FConstSharedFragmentView& Element) { return Element.Requirement.StructType == &SharedFragmentType; });
		return FoundSharedFragmentData ? FoundSharedFragmentData->FragmentView.GetMemory() : nullptr;
	}
	
	/** 模板版只读 shared fragment 指针。要求 T 派生自 FMassConstSharedFragment。 */
	template<typename T>
	const T* GetConstSharedFragmentPtr() const
	{
		static_assert(UE::Mass::CConstSharedFragment<T>, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");

		const FConstSharedFragmentView* FoundSharedFragmentData = ConstSharedFragmentViews.FindByPredicate([](const FConstSharedFragmentView& Element) { return Element.Requirement.StructType == T::StaticStruct(); });
		return FoundSharedFragmentData ? FoundSharedFragmentData->FragmentView.GetPtr<const T>() : static_cast<const T*>(nullptr);
	}

	/** 引用版；找不到 check 崩。 */
	template<typename T>
	const T& GetConstSharedFragment() const
	{
		const T* SharedFragment = GetConstSharedFragmentPtr<const T>();
		CHECK_IF_VALID(SharedFragment, T::StaticStruct());
		return *SharedFragment;
	}

	/**
	 * 可写 shared fragment 指针。
	 * 要求 T 派生自 FMassSharedFragment，且 query 声明为 ReadWrite。
	 * @note 写 shared fragment 会影响所有共享同一实例的 archetype；调用方需自行保证线程/语义安全。
	 */
	template<typename T>
	T* GetMutableSharedFragmentPtr()
	{
		static_assert(UE::Mass::CSharedFragment<T>, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");

		FSharedFragmentView* FoundSharedFragmentData = SharedFragmentViews.FindByPredicate([](const FSharedFragmentView& Element) { return Element.Requirement.StructType == T::StaticStruct(); });
		CHECK_IF_READWRITE(FoundSharedFragmentData);
		return FoundSharedFragmentData ? FoundSharedFragmentData->FragmentView.GetPtr<T>() : static_cast<T*>(nullptr);
	}

	/** 只读版本。 */
	template<typename T>
	const T* GetSharedFragmentPtr() const
	{
		static_assert(UE::Mass::CSharedFragment<T>, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");

		const FSharedFragmentView* FoundSharedFragmentData = SharedFragmentViews.FindByPredicate([](const FSharedFragmentView& Element) { return Element.Requirement.StructType == T::StaticStruct(); });
		return FoundSharedFragmentData ? FoundSharedFragmentData->FragmentView.GetPtr<T>() : static_cast<const T*>(nullptr);
	}

	/** 引用版可写 shared fragment；找不到 check 崩。 */
	template<typename T>
	T& GetMutableSharedFragment()
	{
		T* SharedFragment = GetMutableSharedFragmentPtr<T>();
		CHECK_IF_VALID(SharedFragment, T::StaticStruct());
		return *SharedFragment;
	}

	/** 引用版只读 shared fragment。 */
	template<typename T>
	const T& GetSharedFragment() const
	{
		const T* SharedFragment = GetSharedFragmentPtr<T>();
		CHECK_IF_VALID(SharedFragment, T::StaticStruct());
		return *SharedFragment;
	}

	/* Fragments related operations */
	//-----------------------------------------------------------------------------
	// 普通 fragment 访问（每 entity 一份；视图覆盖整个 chunk 的列）
	//-----------------------------------------------------------------------------
	/**
	 * 取可写 fragment 视图（覆盖当前 chunk 的整列）。
	 * 返回 TArrayView<TFragment>：长度即 chunk 内 entity 数；不能持有越过 chunk 切换。
	 * 校验：query 必须以 ReadWrite 声明该 fragment。
	 */
	template<typename TFragment>
	TArrayView<TFragment> GetMutableFragmentView()
	{
		const UScriptStruct* FragmentType = TFragment::StaticStruct();
		const FFragmentView* View = FragmentViews.FindByPredicate([FragmentType](const FFragmentView& Element) { return Element.Requirement.StructType == FragmentType; });
		CHECK_IF_VALID(View, FragmentType);
		CHECK_IF_READWRITE(View);
		// 强制类型转换：FragmentView 内部保存的是 FMassFragment*，外面要按具体 TFragment 重新解释。
		// 安全前提：所有 fragment 派生都是 POD 标记结构，且当前 chunk 列内每个元素正好是 sizeof(TFragment)。
		return MakeArrayView<TFragment>((TFragment*)View->FragmentView.GetData(), View->FragmentView.Num());
	}

	/** 只读 fragment 视图（按模板类型）。 */
	template<typename TFragment>
	TConstArrayView<TFragment> GetFragmentView() const
	{
		const UScriptStruct* FragmentType = TFragment::StaticStruct();
		const FFragmentView* View = FragmentViews.FindByPredicate([FragmentType](const FFragmentView& Element) { return Element.Requirement.StructType == FragmentType; });
		CHECK_IF_VALID(View, TFragment::StaticStruct());
		return TConstArrayView<TFragment>((const TFragment*)View->FragmentView.GetData(), View->FragmentView.Num());
	}

	/** 反射式只读视图（基于 UScriptStruct*）；返回 FMassFragment 基类视图。 */
	TConstArrayView<FMassFragment> GetFragmentView(const UScriptStruct* FragmentType) const
	{
		const FFragmentView* View = FragmentViews.FindByPredicate([FragmentType](const FFragmentView& Element) { return Element.Requirement.StructType == FragmentType; });
		CHECK_IF_VALID(View, FragmentType);
		return TConstArrayView<FMassFragment>((const FMassFragment*)View->FragmentView.GetData(), View->FragmentView.Num());;
	}

	/** 反射式可写视图。 */
	TArrayView<FMassFragment> GetMutableFragmentView(const UScriptStruct* FragmentType) 
	{
		const FFragmentView* View = FragmentViews.FindByPredicate([FragmentType](const FFragmentView& Element) { return Element.Requirement.StructType == FragmentType; });
		CHECK_IF_VALID(View, FragmentType);
		CHECK_IF_READWRITE(View);
		return View->FragmentView;
	}

	/**
	 * 反射 + 静态基类约束的只读视图。
	 * 用于"运行时 FragmentType 未知，但调用方知道它必派生自 TFragmentBase"的场景：
	 *   先 check IsChildOf 保证类型层次正确，再 reinterpret_cast 到基类视图。
	 */
	template<typename TFragmentBase>
	TConstArrayView<TFragmentBase> GetFragmentView(TNotNull<const UScriptStruct*> FragmentType) const
	{
		check(FragmentType->IsChildOf(TFragmentBase::StaticStruct()));
		TConstArrayView<FMassFragment> View = GetFragmentView(FragmentType);
		return TConstArrayView<TFragmentBase>(reinterpret_cast<const TFragmentBase*>(View.GetData()), View.Num());
	}

	/** 同上但可写。 */
	template<typename TFragmentBase>
	TArrayView<TFragmentBase> GetMutableFragmentView(TNotNull<const UScriptStruct*> FragmentType)
	{
		check(FragmentType->IsChildOf(TFragmentBase::StaticStruct()));
		TArrayView<FMassFragment> View = GetMutableFragmentView(FragmentType);
		return TArrayView<TFragmentBase>(reinterpret_cast<TFragmentBase*>(View.GetData()), View.Num());;
	}

	//-----------------------------------------------------------------------------
	// Subsystem 访问（转发到 SubsystemAccess）
	// SFINAE 限制：T 必须派生自 USubsystem。详细注释见 FMassSubsystemAccess。
	//-----------------------------------------------------------------------------
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T* GetMutableSubsystem()
	{
		return SubsystemAccess.GetMutableSubsystem<T>();
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T& GetMutableSubsystemChecked()
	{
		return SubsystemAccess.GetMutableSubsystemChecked<T>();
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T* GetSubsystem()
	{
		return SubsystemAccess.GetSubsystem<T>();
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T& GetSubsystemChecked()
	{
		return SubsystemAccess.GetSubsystemChecked<T>();
	}

	/** 运行时类型版（SubsystemClass 在运行时给出）。 */
	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T* GetMutableSubsystem(const TSubclassOf<USubsystem> SubsystemClass)
	{
		return SubsystemAccess.GetMutableSubsystem<T>(SubsystemClass);
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	T& GetMutableSubsystemChecked(const TSubclassOf<USubsystem> SubsystemClass)
	{
		return SubsystemAccess.GetMutableSubsystemChecked<T>(SubsystemClass);
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T* GetSubsystem(const TSubclassOf<USubsystem> SubsystemClass)
	{
		return SubsystemAccess.GetSubsystem<T>(SubsystemClass);
	}

	template<typename T, typename = typename TEnableIf<TIsDerivedFrom<T, USubsystem>::IsDerived>::Type>
	const T& GetSubsystemChecked(const TSubclassOf<USubsystem> SubsystemClass)
	{
		return SubsystemAccess.GetSubsystemChecked<T>(SubsystemClass);
	}

	/** Sparse chunk related operation */
	/** 取稀疏 chunk 集合（如有）。query 在执行时用它来过滤要遍历的 chunk。 */
	const FMassArchetypeEntityCollection& GetEntityCollection() const { return EntityCollection; }

	/** 取/取可变 AuxData。 */
	const FInstancedStruct& GetAuxData() const { return AuxData; }
	FInstancedStruct& GetMutableAuxData() { return AuxData; }
	
	/**
	 * 校验 AuxData 实际类型是否为 TFragment。
	 * 典型用法：在 lambda 入口处 check Context.ValidateAuxDataType<MyPayload>()，再安全地读取 AuxData 内容。
	 */
	template<typename TFragment>
	bool ValidateAuxDataType() const
	{
		const UScriptStruct* FragmentType = GetAuxData().GetScriptStruct();
		return FragmentType != nullptr && FragmentType == TFragment::StaticStruct();
	}

	/**
	 * 把 DeferredCommandBuffer 中累积的延迟命令一次性提交回 EntityManager 执行。
	 * - 仅当 bFlushDeferredCommands == true 且 buffer 非空时才会真正 flush。
	 * - 一般在 query 一组执行结束、下一组结构性修改可能开始之前调用。
	 */
	MASSENTITY_API void FlushDeferred();

	/**
	 * 重置当前 chunk 相关的所有状态（fragment views、entity list、archetype 描述、chunk 修改序号、debug 颜色）。
	 * 调用时机：archetype 切换或本次 query 完全结束时。
	 */
	void ClearExecutionData();
	/** 设置当前 chunk 所属 archetype 的组成描述。供 DoesArchetypeHaveFragment/Tag 查询。 */
	void SetCurrentArchetypeCompositionDescriptor(const FMassArchetypeCompositionDescriptor& Descriptor)
	{
		CurrentArchetypeCompositionDescriptor = Descriptor;
	}

#if WITH_MASSENTITY_DEBUG
	/** 调试：设置当前 archetype 在 visualizer 中的标识颜色。 */
	void DebugSetColor(FColor InColor)
	{
		DebugColor = InColor;
	}
#endif // WITH_MASSENTITY_DEBUG

	/** 
	 * Processes SubsystemRequirements to fetch and cache all the indicated subsystems. If a UWorld is required to fetch
	 * a specific subsystem then the one associated with the stored EntityManager will be used.
	 *
	 * @param SubsystemRequirements indicates all the subsystems that are expected to be accessed. Requesting a subsystem 
	 *	not indicated by the SubsystemRequirements will result in a failure.
	 * 
	 * @return `true` if all required subsystems have been found, `false` otherwise.
	 */
	/**
	 * 处理 query 的 SubsystemRequirements：fetch 并缓存指定 subsystem 指针，更新位掩码。
	 * 调用时机：query 第一次执行（或 query 切换）时；之后整段 chunk 迭代里 GetSubsystem<T> 都直接命中缓存。
	 *
	 * @param SubsystemRequirements 列出本 query 期望访问的 subsystem；越界访问会触发 ensure。
	 * @return 全部 fetch 成功返回 true；任意失败返回 false（位掩码不会被改写）。
	 */
	bool CacheSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements);

protected:
	/** 仅设置 SubsystemAccess 的位掩码（不真正 fetch），用于继承场景。 */
	void SetSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
	{
		SubsystemAccess.SetSubsystemRequirements(SubsystemRequirements);
	}

	/**
	 * 根据 query 的 FragmentRequirements 重建 4 类 FragmentView 容器条目（仅 Requirement 部分；
	 * 真正的 view 在 chunk bind 时由 Archetype 代码填）。详见 .cpp 实现。
	 */
	void SetFragmentRequirements(const FMassFragmentRequirements& FragmentRequirements);

	/**
	 * 把所有 4 类 FragmentView 的 view 部分清空（保留 Requirement 描述，便于下个 chunk 复用）。
	 * 注意区分：
	 *  - 私有 ClearFragmentViews()：纯清空 view，不做 query 校验，框架内部用。
	 *  - public ClearFragmentViews(const FMassEntityQuery&)：转发到此函数，但额外校验栈顶 query。
	 */
	void ClearFragmentViews()
	{
		for (FFragmentView& View : FragmentViews)
		{
			View.FragmentView = TArrayView<FMassFragment>();
		}
		for (FChunkFragmentView& View : ChunkFragmentViews)
		{
			View.FragmentView.Reset();
		}
		for (FConstSharedFragmentView& View : ConstSharedFragmentViews)
		{
			View.FragmentView.Reset();
		}
		for (FSharedFragmentView& View : SharedFragmentViews)
		{
			View.FragmentView.Reset();
		}
	}
};

//-----------------------------------------------------------------------------
// Inlines
// 简单的内联 getter/setter，避免函数调用开销。
//-----------------------------------------------------------------------------
inline FMassEntityManager& FMassExecutionContext::GetEntityManagerChecked() const
{
	return EntityManager.Get();
}

inline const TSharedRef<FMassEntityManager>& FMassExecutionContext::GetSharedEntityManager()
{
	return EntityManager;
}

inline void FMassExecutionContext::SetFlushDeferredCommands(const bool bNewFlushDeferredCommands)
{
	bFlushDeferredCommands = bNewFlushDeferredCommands;
}

inline void FMassExecutionContext::SetDeferredCommandBuffer(const TSharedPtr<FMassCommandBuffer>& InDeferredCommandBuffer)
{
	DeferredCommandBuffer = InDeferredCommandBuffer;
}

inline void FMassExecutionContext::ClearEntityCollection()
{
	EntityCollection.Reset();
}

inline void FMassExecutionContext::SetAuxData(const FInstancedStruct& InAuxData)
{
	AuxData = InAuxData;
}

inline float FMassExecutionContext::GetDeltaTimeSeconds() const
{
	return DeltaTimeSeconds;
}

inline void FMassExecutionContext::SetExecutionType(EMassExecutionContextType InExecutionType)
{
	check(InExecutionType != EMassExecutionContextType::MAX);
	ExecutionType = InExecutionType;
}

inline EMassExecutionContextType FMassExecutionContext::GetExecutionType() const
{
	return ExecutionType;
}

/** 必须栈非空（check）；返回当前栈顶 query 引用。 */
inline const FMassEntityQuery& FMassExecutionContext::GetCurrentQuery() const
{
	check(QueriesStack.Num());
	return *QueriesStack.Last().Query;
}

inline bool FMassExecutionContext::IsCurrentQuery(const FMassEntityQuery& Query) const
{
	return QueriesStack.Num() && QueriesStack.Last().Query == &Query;
}

/** 仅栈顶 query 自己可调用，用于把它的 FragmentRequirement 应用到 FragmentViews 容器中。 */
inline void FMassExecutionContext::ApplyFragmentRequirements(const FMassEntityQuery& RequestingQuery)
{
	check(IsCurrentQuery(RequestingQuery));
	SetFragmentRequirements(RequestingQuery);
}

/** 仅栈顶 query 自己可调用，用于在 chunk 切换时把所有 view 置空。 */
inline void FMassExecutionContext::ClearFragmentViews(const FMassEntityQuery& RequestingQuery)
{
	check(IsCurrentQuery(RequestingQuery));
	ClearFragmentViews();
}

#undef CHECK_IF_VALID
#undef CHECK_IF_READWRITE
