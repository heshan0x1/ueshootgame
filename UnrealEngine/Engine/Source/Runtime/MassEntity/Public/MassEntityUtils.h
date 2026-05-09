// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#include "MassArchetypeTypes.h"

class UWorld;
struct FMassEntityManager;
struct FMassEntityHandle;

namespace UE::Mass::Utils
{

/** returns the current execution mode for the processors calculated from the world network mode */
/**
 * 中文说明：
 *   把 UWorld 的网络模式 (NetMode) 翻译成 Mass processor 的执行标志 (EProcessorExecutionFlags)，
 *   决定本世界中哪些 processor 应该被实例化和执行：
 *     - NM_DedicatedServer  -> Server
 *     - NM_ListenServer     -> Server | Client（监听服务器同时承担二者）
 *     - NM_Client           -> Client
 *     - NM_Standalone       -> Standalone
 *     - 编辑器世界（非 Game）-> EditorWorld（仅 WITH_EDITOR 时返回）
 *   未识别的 NetMode 视为编程错误，会 check fail。
 */
MASSENTITY_API extern EProcessorExecutionFlags GetProcessorExecutionFlagsForWorld(const UWorld& World);

/** based on the given World (which can be null) and 'ExecutionFlagsOverride', the function determines the execution flags to use */
/**
 * 中文说明：
 *   决策最终的 execution flags，按优先级：
 *     1) ExecutionFlagsOverride != None    -> 直接使用 override；
 *     2) World 非空                         -> 走 GetProcessorExecutionFlagsForWorld(World)；
 *     3) WITH_EDITOR 且 GEditor 存在        -> EProcessorExecutionFlags::Editor；
 *     4) 兜底                                -> EProcessorExecutionFlags::All（最宽松）。
 *   适合在没有 World 的工具上下文（CLI、commandlet、editor 启动早期）下安全地拿到一个合理 flag。
 */
MASSENTITY_API extern EProcessorExecutionFlags DetermineProcessorExecutionFlags(const UWorld* World, EProcessorExecutionFlags ExecutionFlagsOverride = EProcessorExecutionFlags::None);

/** based on the given World (which can be null), the function determines additional level tick types for the processing phases */
/**
 * 中文说明：
 *   决定 processor phase 应在哪些 LEVELTICK_* 类型下被调度。返回 bitfield (1<<ELevelTick)。
 *   - 编辑器世界：返回 MAX_uint8（全开），允许编辑器场景下也能 tick；
 *   - 否则：返回 (1<<LEVELTICK_All) | (1<<LEVELTICK_TimeOnly)（标准游戏 tick + 暂停状态下仅时间 tick）。
 */
uint8 DetermineProcessorSupportedTickTypes(const UWorld* World);

/** 
 * Fills OutEntityCollections with per-archetype FMassArchetypeEntityCollection instances. 
 * @param DuplicatesHandling used to inform the function whether to expect duplicates.
 *
 * 中文说明：
 *   把任意 entity handle 数组按 archetype 分桶，产出一组 FMassArchetypeEntityCollection。
 *   这是 FEntityCollection 的底层支撑函数，也可独立使用。
 *   - 流程：
 *       a) 遍历 Entities，对每个 valid handle 调 GetArchetypeForEntityUnsafe 拿到 archetype；
 *       b) 用 TMap<archetype, TArray<handle>> 做分桶；
 *       c) 每个桶产出一个 FMassArchetypeEntityCollection。
 *   - DuplicatesHandling：
 *       - NoDuplicates：调用方保证无重复，构造 collection 时跳过去重；
 *       - FoldDuplicates：内部排序 + 去重，更慢但安全。
 */
MASSENTITY_API extern void CreateEntityCollections(const FMassEntityManager& EntityManager, const TConstArrayView<FMassEntityHandle> Entities
	, const FMassArchetypeEntityCollection::EDuplicatesHandling DuplicatesHandling, TArray<FMassArchetypeEntityCollection>& OutEntityCollections);

/**
* AbstractSort is a sorting function that only needs to know how many items there are, how to compare items
* at individual locations - where location is in [0, NumElements) - and how to swap two elements at given locations.
* The main use case is to sort multiple arrays while keeping them in sync. For example:
*
* TArray<float> Lead = { 3.1, 0.2, 2.6, 1.0 };
* TArray<UObject*> Payload = { A, B, C, D };
*
* AbstractSort(Lead.Num()											// NumElements
* 	, [&Lead](const int32 LHS, const int32 RHS)					// Predicate
*		{
*			return Lead[LHS] < Lead[RHS];
*		}
* 	, [&Lead, &Payload](const int32 A, const int32 B)			// SwapFunctor
*	 	{
*			Swap(Lead[A], Lead[B]);
* 			Swap(Payload[A], Payload[B]);
*		}
* );
*
* 中文说明：
*   "抽象排序"——只需提供 (元素个数, 比较谓词(LHS,RHS), 交换函子(A,B))，不要求实际数据放在哪。
*   核心用途是 **让多个并行数组保持同步排序**——这是 ECS / 列式存储的常见诉求：
*     例如 archetype 内每个 fragment 是独立的列，要按某个 key 列排序时，必须同时搬动其它列的值，
*     直接用 TArray::Sort 做不到，因为它假设元素是单一容器内的连续值。
*
*   算法（间接排序 + 周期分解写回）：
*     1) 构造索引数组 Indices = [0, 1, ..., N-1]；
*     2) 用 Predicate 对 Indices 做排序——结果 Indices[i] 表示"排序后第 i 位应当来自原始位置 Indices[i]"；
*     3) **周期分解** 写回：对每个目标位置 i，沿着 Indices 链下溯，跳过 < i 的位置（那些位置已经在前面写完了），
*        最终 SwapFromIndex 是真正应该被搬到 i 的"当前残余位置"——执行 SwapFunctor(i, SwapFromIndex)。
*     4) 整个过程对每个置换环 (cycle) 只做长度-1 次 swap，达到 O(N) 次 swap 的最优搬动量。
*
*   注意：SwapFunctor 必须能就地交换 *所有* 列，否则数据会错位。
*/
template<typename TPred, typename TSwap>
inline void AbstractSort(const int32 NumElements, TPred&& Predicate, TSwap&& SwapFunctor)
{
	if (NumElements == 0)
	{
		return;
	}

	// 中文：用 do-while 是为了让 N>=1 时少做一次循环条件判断（已知首次必进入）。
	TArray<int32> Indices;
	Indices.AddUninitialized(NumElements);
	int i = 0;
	do
	{
		Indices[i] = i;
	} while (++i < NumElements);

	// 中文：对索引数组做"间接"排序——Indices.Sort 用提供的 Predicate 比较 (LHS, RHS) 这两个原始位置，
	//       得到的 Indices[i] 描述"排序后第 i 个名次的元素，原本来自哪里"。
	Indices.Sort(Predicate);

	// 中文：周期分解写回——下面这段是实现"按 Indices 重排但只用 Swap"的经典算法。
	//   假设 Indices = [2, 0, 1]，意为 result[0]=src[2], result[1]=src[0], result[2]=src[1]。
	//   - i=0：SwapFromIndex 起始 = Indices[0] = 2，>0，无需下溯，Swap(0, 2)；
	//   - i=1：SwapFromIndex = Indices[1] = 0，0 < 1，下溯：SwapFromIndex = Indices[0] = 2，>=1，Swap(1, 2)；
	//   - i=2：SwapFromIndex = Indices[2] = 1，<2，下溯：SwapFromIndex = Indices[1] = 0，<2 还要下溯：
	//          SwapFromIndex = Indices[0] = 2，==i 跳过 swap。
	for (i = 0; i < NumElements; ++i)
	{
		int32 SwapFromIndex = Indices[i];
		// 中文：若链头指向已写好的位置（SwapFromIndex < i），按"链表"下溯到当前真正持有该原始值的位置。
		while (SwapFromIndex < i)
		{
			SwapFromIndex = Indices[SwapFromIndex];
		}

		if (SwapFromIndex != i)
		{
			SwapFunctor(i, SwapFromIndex);
		}
	}
}

/**
 * 中文说明：从 UObject 的所属 World 取默认 EntityManager。
 *   实现：WorldContextObject->GetWorld() -> UWorld::GetSubsystem<UMassEntitySubsystem>() -> GetMutableEntityManager()。
 *   返回 nullptr 表示：context 没有 World 或 World 上没有 MassEntitySubsystem。
 *   注意：项目中可能存在多个 EntityManager（subsystem 之外另起的实例），此函数仅返回 subsystem 上的"默认"那个。
 *   多 manager 共存场景需要调用方自己管理引用。
 */
MASSENTITY_API extern FMassEntityManager* GetEntityManager(const UObject* WorldContextObject);
/** 中文：UWorld* 重载，逻辑同上。*/
MASSENTITY_API extern FMassEntityManager* GetEntityManager(const UWorld* World);
/** 中文：Checked 版本——subsystem 必须存在，否则 check fail。返回引用而非指针，调用方无需判空。*/
MASSENTITY_API extern FMassEntityManager& GetEntityManagerChecked(const UWorld& World);

} // namespace UE::Mass::Utils

