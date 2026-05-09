// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassEntityManagerStorage.cpp
// -----------------------------------------------------------------------------
// 实体存储后端的具体实现。两套实现共存：
//   - FSingleThreadedEntityStorage：TChunkedArray + free-list 数组，无锁。
//   - FConcurrentEntityStorage   ：分页存储 + 互斥量，多线程 Acquire/Release。
// 所有"线程安全"的描述见头文件类注释。
// =============================================================================

#include "MassEntityManagerStorage.h"
#include "MassEntityManagerConstants.h"
#include "MassEntityHandle.h"
#include "MassEntityTypes.h"
#include "Templates/SharedPointer.h"

namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// IEntityStorageInterface
	//-----------------------------------------------------------------------------
	/**
	 * 便利重载：把"批量分配请求"翻译成对子类 Acquire(TArrayView) 的一次调用。
	 *
	 * 流程：
	 *   1) 在 OutEntityHandles 末尾追加 Count 个零初始化槽；
	 *   2) 调用虚函数 Acquire(view)，让子类按其线程模型实际填充；
	 *   3) 如果子类返回 NumberAdded < Count（OOM 或耗尽），把多余的槽截掉。
	 *
	 * 线程安全性继承自所属子类的 Acquire(TArrayView)。
	 */
	int32 IEntityStorageInterface::Acquire(const int32 Count, TArray<FMassEntityHandle>& OutEntityHandles)
	{
		if (Count)
		{
			const int32 StartingIndex = OutEntityHandles.Num();
			OutEntityHandles.AddZeroed(Count);
			const int32 NumberAdded = Acquire(MakeArrayView(&OutEntityHandles[StartingIndex], Count));
			if (UNLIKELY(NumberAdded < Count))
			{
				// need to remove the redundantly reserved entries
				// 子类未填满全部请求 → 把尾部多余的零槽移除（不缩容，避免下一次再扩展）。
				OutEntityHandles.RemoveAt(StartingIndex + NumberAdded, Count - NumberAdded, EAllowShrinking::No);
			}
			return NumberAdded;
		}
		return 0;
	}

	//-----------------------------------------------------------------------------
	// FSingleThreadedEntityStorage
	//-----------------------------------------------------------------------------

	/**
	 * 单线程后端初始化：占据 Index = 0 作为"哨兵实体"。这样
	 * UE::Mass::Private::InvalidEntityIndex (= 0) 就永远不会被对外发出，
	 * 任何使用 Handle.Index == 0 的代码就能用作"无效句柄"哨兵。
	 *
	 * 调用线程：构造期 / Manager 初始化阶段，单线程。
	 */
	void FSingleThreadedEntityStorage::Initialize(const FMassEntityManager_InitParams_SingleThreaded&)
	{
		// Index 0 is reserved so we can treat that index as an invalid entity handle
		// Index 0 被保留作为无效句柄哨兵，确保 AcquireOne 永远不会发出 Index=0 的合法实体。
		const FMassEntityHandle SentinelEntity = AcquireOne();
		check(SentinelEntity.Index == UE::Mass::Private::InvalidEntityIndex);
	}

	// ----- 简单访问器 -----------------------------------------------------------
	// 单线程后端不需要任何同步，直接索引 TChunkedArray。
	// 注意：Entities[Index] 在 TChunkedArray 上是 O(1) 的 chunk-base + offset 访问。

	FMassArchetypeData* FSingleThreadedEntityStorage::GetArchetype(int32 Index)
	{
		return Entities[Index].CurrentArchetype.Get();
	}

	const FMassArchetypeData* FSingleThreadedEntityStorage::GetArchetype(int32 Index) const
	{
		return Entities[Index].CurrentArchetype.Get();
	}

	TSharedPtr<FMassArchetypeData>& FSingleThreadedEntityStorage::GetArchetypeAsShared(int32 Index)
	{
		return Entities[Index].CurrentArchetype;
	}

	const TSharedPtr<FMassArchetypeData>& FSingleThreadedEntityStorage::GetArchetypeAsShared(int32 Index) const
	{
		return Entities[Index].CurrentArchetype;
	}

	void FSingleThreadedEntityStorage::SetArchetypeFromShared(int32 Index, TSharedPtr<FMassArchetypeData>& Archetype)
	{
		Entities[Index].CurrentArchetype = Archetype;
	}

	void FSingleThreadedEntityStorage::SetArchetypeFromShared(int32 Index, const TSharedPtr<FMassArchetypeData>& Archetype)
	{
		Entities[Index].CurrentArchetype = Archetype;
	}

	/**
	 * 状态判定：
	 *   Serial == 0  → Free（这是单线程后端的状态编码：0 表示无效）
	 *   Serial != 0  + Archetype 为空 → Reserved（已分配 Index 但未绑 archetype）
	 *   Serial != 0  + Archetype 非空 → Created
	 *
	 * 注意：本方法读取的 SerialNumber 类型为 int32，但局部变量声明为 uint32 —— 实际作用
	 * 只是和 0 比较，不影响正确性，但风格上稍有不一致。
	 */
	IEntityStorageInterface::EEntityState FSingleThreadedEntityStorage::GetEntityState(int32 Index) const
	{
		const uint32 CurrentSerialNumber = Entities[Index].SerialNumber;

		if (CurrentSerialNumber != 0)
		{
			return Entities[Index].CurrentArchetype.Get()
				? EEntityState::Created 
				: EEntityState::Reserved;
		}

		return EEntityState::Free;	
	}

	int32 FSingleThreadedEntityStorage::GetSerialNumber(int32 Index) const
	{
		return Entities[Index].SerialNumber;
	}

	bool FSingleThreadedEntityStorage::IsValidIndex(int32 Index) const
	{
		return Entities.IsValidIndex(Index);
	}

	/**
	 * 句柄合法性检查：Index 在范围内，且槽内 Serial 与 Handle.Serial 完全匹配。
	 * 这是抵御"悬垂句柄"的核心：旧实体被释放后，新实体复用同一 Index 时 Serial 已递增，
	 * 旧句柄此处会比对失败。
	 */
	bool FSingleThreadedEntityStorage::IsValidHandle(FMassEntityHandle EntityHandle) const
	{
		return Entities.IsValidIndex(EntityHandle.Index)
			&& Entities[EntityHandle.Index].SerialNumber == EntityHandle.SerialNumber;
	}

	/** 句柄合法 + 状态必须为 Created。Reserved 阶段调用本函数会返回 false。 */
	bool FSingleThreadedEntityStorage::IsEntityActive(FMassEntityHandle EntityHandle) const
	{
		return IsValidIndex(EntityHandle.Index)
			&& GetSerialNumber(EntityHandle.Index) == EntityHandle.SerialNumber
			&& GetEntityState(EntityHandle.Index) == UE::Mass::IEntityStorageInterface::EEntityState::Created;
	}

	SIZE_T FSingleThreadedEntityStorage::GetAllocatedSize() const
	{
		return Entities.GetAllocatedSize() + EntityFreeIndexList.GetAllocatedSize();
	}

	bool FSingleThreadedEntityStorage::IsValid(int32 Index) const
	{
		return Entities[Index].IsValid();
	}

	/**
	 * 单实体分配：
	 *   1) 生成新 Serial（全局原子自增）；
	 *   2) 优先从 free-list 取空 Index（LIFO，最近释放的优先复用，cache 友好）；
	 *      若 free-list 空，则在 Entities 末尾新增一个槽。
	 *   3) 把新 Serial 写入槽。
	 *
	 * Free → Reserved 状态过渡。返回的 Handle 中 Index>=1（0 被哨兵占住）。
	 *
	 * 线程约束：仅主线程。
	 */
	FMassEntityHandle FSingleThreadedEntityStorage::AcquireOne()
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/SingleThreadedStorage"));
		const int32 SerialNumber = GenerateSerialNumber();
		const int32 Index = (EntityFreeIndexList.Num() > 0) ? EntityFreeIndexList.Pop(EAllowShrinking::No) : Entities.Add();
		Entities[Index].SerialNumber = SerialNumber;

		FMassEntityHandle Handle;
		Handle.SerialNumber = SerialNumber;
		Handle.Index = Index;
		return Handle;
	}

	/**
	 * 批量分配。性能优化点：
	 *   - 整批共用同一个 SerialNumber（只 fetch_add 一次）。同一帧内同一来源的实体序号一致，
	 *     有利于后续按 Serial 聚类查询。
	 *   - 优先消费 free-list 末尾的连续段（对应的内存最热），不够再一次 Entities.Add(N) 扩展。
	 *
	 * 注意细节：
	 *   - free-list 拷贝顺序是"从 FirstIndexToUse 开始向后遍历"，即按 free-list 中的存放顺序
	 *     而非弹栈顺序输出 —— 由于后面紧跟 RemoveAt(FirstIndexToUse, NumAvailableFromFreeList)
	 *     一次性截断，从语义上等价于"批量 Pop 末尾 N 项"。
	 *   - Entities.Add(N) 在 TChunkedArray 上分摊 O(N)，且不会让旧引用失效（关键点）。
	 *
	 * 返回：实际分配数量（单线程实现下永远等于 NumToAdd）。
	 */
	int32 FSingleThreadedEntityStorage::Acquire(TArrayView<FMassEntityHandle> OutEntityHandles)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/SingleThreadedStorage"));
		const int32 NumToAdd = OutEntityHandles.Num();

		// 整批共用一个 Serial，节省 N-1 次原子自增。
		const int32 SerialNumber = GenerateSerialNumber();

		int32 NumAdded = 0;
		int32 CurrentEntityHandleIndex = 0;

		// ----- 第一阶段：从 free-list 末尾批量复用 Index -----
		const int32 NumAvailableFromFreeList = FMath::Min(NumToAdd, EntityFreeIndexList.Num());
		if (NumAvailableFromFreeList > 0)
		{
			const int32 FirstIndexToUse = EntityFreeIndexList.Num() - NumAvailableFromFreeList;
			for (int32 Index = FirstIndexToUse; Index < EntityFreeIndexList.Num(); ++Index)
			{
				const int32 EntityIndex = EntityFreeIndexList[Index];
				Entities[EntityIndex].SerialNumber = SerialNumber;
				OutEntityHandles[CurrentEntityHandleIndex++] = { EntityIndex, SerialNumber };
			}
			// 一次性截断 free-list 尾部，避免逐项 Pop 的 O(N) 写。
			EntityFreeIndexList.RemoveAt(FirstIndexToUse, NumAvailableFromFreeList, EAllowShrinking::No);
			NumAdded = NumAvailableFromFreeList;
		}

		// ----- 第二阶段：free-list 不够 → 在 Entities 末尾追加 -----
		if (NumAdded < NumToAdd)
		{
			const int32 RemainingCount = NumToAdd - NumAdded;
			const int32 StartingIndex = Entities.Num();
			Entities.Add(RemainingCount);
			for (int32 EntityIndex = StartingIndex; EntityIndex < Entities.Num(); ++EntityIndex)
			{
				Entities[EntityIndex].SerialNumber = SerialNumber;
				OutEntityHandles[CurrentEntityHandleIndex++] = { EntityIndex, SerialNumber };
			}
			NumAdded += RemainingCount;
		}

		return NumAdded;
	}

	/**
	 * 批量释放（带 Serial 校验）：仅释放 Serial 匹配的句柄。
	 *   - 不匹配的句柄被静默跳过（说明已被前面的 Release 释放过，或来自旧版本句柄）。
	 *   - 对每个匹配的槽：Reset() 清空 archetype 引用 + Serial 置 0；Index 入 free-list。
	 *
	 * 释放后槽进入 Free 状态。注意：Serial 在 Reset 中清零 → 之后再 Acquire 时使用的是
	 * 新的全局 Serial（递增），与本次释放的旧 Serial 不会重合。
	 *
	 * 返回真实释放数量。线程：仅主线程。
	 */
	int32 FSingleThreadedEntityStorage::Release(TConstArrayView<FMassEntityHandle> Handles)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/SingleThreadedStorage"));
		int DeallocateCount = 0;

		// 一次性 Reserve，避免 free-list 多次扩容。
		EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + Handles.Num());

		for (const FMassEntityHandle& Handle : Handles)
		{
			FEntityData& EntityData = Entities[Handle.Index];
			if (EntityData.SerialNumber == Handle.SerialNumber)
			{
				EntityData.Reset();
				EntityFreeIndexList.Add(Handle.Index);
				++DeallocateCount;
			}
		}
	
		return DeallocateCount;
	}

	int32 FSingleThreadedEntityStorage::ReleaseOne(FMassEntityHandle Handle)
	{
		return Release(MakeArrayView(&Handle, 1));
	}

	/**
	 * 强制释放（跳过 Serial 校验）。仅在调用方"绝对确定句柄合法"时使用 —— 滥用会
	 * 释放别人正在持有的实体，导致悬垂引用穿透 Serial 防御。
	 */
	int32 FSingleThreadedEntityStorage::ForceRelease(TConstArrayView<FMassEntityHandle> Handles)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/SingleThreadedStorage"));
		EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + Handles.Num());
		for (const FMassEntityHandle& Handle : Handles)
		{
			FEntityData& EntityData = Entities[Handle.Index];
			EntityData.Reset();
			EntityFreeIndexList.Add(Handle.Index);
		}
		return Handles.Num();
	}

	int32 FSingleThreadedEntityStorage::ForceReleaseOne(FMassEntityHandle Handle)
	{
		return ForceRelease(MakeArrayView(&Handle, 1));
	}

	/** 已分配的槽总数（含 Free 与 Created）。注意不是"活跃实体数"。 */
	int32 FSingleThreadedEntityStorage::Num() const
	{
		return Entities.Num();
	}

	int32 FSingleThreadedEntityStorage::ComputeFreeSize() const
	{
		return EntityFreeIndexList.Num();
	}

	//-----------------------------------------------------------------------------
	// FSingleThreadedEntityStorage::FEntityData
	//-----------------------------------------------------------------------------

	FSingleThreadedEntityStorage::FEntityData::~FEntityData() = default;

	/** 把槽清回 Free 状态：archetype 引用 release，Serial 置 0。 */
	void FSingleThreadedEntityStorage::FEntityData::Reset()
	{
		CurrentArchetype.Reset();
		SerialNumber = 0;
	}

	/** "完整实体" = Serial 已分配 + archetype 已绑定。 */
	bool FSingleThreadedEntityStorage::FEntityData::IsValid() const
	{
		return SerialNumber != 0 && CurrentArchetype.IsValid();
	}

	//-----------------------------------------------------------------------------
	// FConcurrentEntityStorage
	//-----------------------------------------------------------------------------

	/**
	 * 初始化并发后端：根据上限计算"页指针表"大小并一次性 malloc。
	 *
	 * 关键不变量：MaxEntityCount / MaxEntitiesPerPage 必须都是 2 的幂，从而支持
	 *   - PageIdx     = Index >> MaxEntitiesPerPageShift
	 *   - InPage      = Index &  ((1<<MaxEntitiesPerPageShift) - 1)
	 * 这样 LookupEntity 不需要除法/取模。
	 *
	 * 注意：本步骤只分配"指针表"，并不分配任何 page；page 在第一次 AcquireOne 时
	 * 由 AddPage() 惰性分配。
	 *
	 * 调用线程：构造期，单线程（Manager 启动时）。
	 */
	void FConcurrentEntityStorage::Initialize(const FMassEntityManager_InitParams_Concurrent& InInitializationParams)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/ConcurrentStorage"));
		// Compute number of pages required
		// 计算所需页数；要求两个上限都必须是 2 的幂，便于位移计算。
		check(FMath::IsPowerOfTwo(InInitializationParams.MaxEntitiesPerPage));
		check(FMath::IsPowerOfTwo(InInitializationParams.MaxEntityCount));
		MaxEntitiesPerPage = InInitializationParams.MaxEntitiesPerPage;
		MaxEntitiesPerPageShift = FMath::FloorLog2(InInitializationParams.MaxEntitiesPerPage);
		MaximumEntityCountShift = FMath::FloorLog2(InInitializationParams.MaxEntityCount);
		// MaxEntityCount 不能超过 31 位（FMassEntityHandle.Index 是 int32，最高位是符号位）。
		checkf(MaximumEntityCountShift < 32, TEXT("Invalid maximum entity count, cannot exceed 31 bits"));

		const uint64 PagePointerCount = InInitializationParams.MaxEntityCount / InInitializationParams.MaxEntitiesPerPage;

		// 一次性分配并清零页指针表；此后该指针本身不会再 realloc，仅"槽位"按需写入。
		const uint64 EntityPageSize = sizeof(void*) * PagePointerCount;
		EntityPages = static_cast<FEntityData**>(FMemory::Malloc(EntityPageSize, alignof(FEntityData**)));
		FMemory::Memzero(EntityPages, EntityPageSize);	
	}

	/**
	 * 析构：依次释放所有已分配的页（page 内 FEntityData 用 Memzero 初始化，
	 * 而非 placement-new，所以也无需逐个调用析构 —— 见 DebugAssumptionsSelfTest）。
	 *
	 * 注意：FEntityData 内的 TSharedPtr 在被 Memzero 重新清零前应已通过
	 * Release/ForceRelease 释放掉强引用，否则这里会泄漏共享所有权。但实际上
	 * Manager 在销毁前会清空所有实体，因此这里不显式调用 Reset。
	 */
	FConcurrentEntityStorage::~FConcurrentEntityStorage()
	{
		if (EntityPages != nullptr)
		{
			for (uint32 Index = 0; Index < PageCount; ++Index)
			{
				FMemory::Free(EntityPages[Index]);
				EntityPages[Index] = nullptr;
			}
			FMemory::Free(EntityPages);
			EntityPages = nullptr;
		}
	}

	// ----- 简单访问器（无锁） ---------------------------------------------------
	// LookupEntity 把 Index 拆成 (PageIdx, InPage) 直接返回引用。前提是 Index 落
	// 在已分配 page 范围内 —— 调用方一般在外层先 IsValidIndex 过滤。

	FMassArchetypeData* FConcurrentEntityStorage::GetArchetype(int32 Index)
	{
		return LookupEntity(Index).CurrentArchetype.Get();
	}

	const FMassArchetypeData* FConcurrentEntityStorage::GetArchetype(int32 Index) const
	{
		return LookupEntity(Index).CurrentArchetype.Get();
	}

	TSharedPtr<FMassArchetypeData>& FConcurrentEntityStorage::GetArchetypeAsShared(int32 Index)
	{
		return LookupEntity(Index).CurrentArchetype;
	}

	const TSharedPtr<FMassArchetypeData>& FConcurrentEntityStorage::GetArchetypeAsShared(int32 Index) const
	{
		return LookupEntity(Index).CurrentArchetype;
	}

	void FConcurrentEntityStorage::SetArchetypeFromShared(int32 Index, TSharedPtr<FMassArchetypeData>& Archetype)
	{
		LookupEntity(Index).CurrentArchetype = Archetype;
	}

	void FConcurrentEntityStorage::SetArchetypeFromShared(int32 Index, const TSharedPtr<FMassArchetypeData>& Archetype)
	{
		LookupEntity(Index).CurrentArchetype = Archetype;
	}

	/**
	 * 状态判定（无锁；可与 Acquire/Release 并发；读取的是瞬时值）。
	 *
	 *   ┌────────────┬────────────────┬─────────────┐
	 *   │ Archetype  │  bIsAllocated  │   状态      │
	 *   ├────────────┼────────────────┼─────────────┤
	 *   │  nullptr   │       0        │   Free      │
	 *   │  nullptr   │       1        │   Reserved  │
	 *   │  非空      │       1        │   Created   │
	 *   └────────────┴────────────────┴─────────────┘
	 *
	 * 这里把"非空 archetype"当作 Created 的最高优先级判据 —— 即使 bIsAllocated 因
	 * 某种 race 暂时不对齐，也能识别出已经绑定了 archetype 的槽。
	 */
	IEntityStorageInterface::EEntityState FConcurrentEntityStorage::GetEntityStateInternal(const FEntityData& EntityData) const
	{
		//
		// || Archetype || bIsAllocated || Result    |
		//  |  nullptr   |      0       |  Free     |
		//  |  nullptr   |      1       |  Reserved |
		//  | !nullptr   |      1       |  Created  |
		//	
		if (EntityData.CurrentArchetype != nullptr)
		{
			return EEntityState::Created;
		}

		return EntityData.bIsAllocated
			? EEntityState::Reserved
			: EEntityState::Free;
	}

	IEntityStorageInterface::EEntityState FConcurrentEntityStorage::GetEntityState(int32 Index) const
	{
		return GetEntityStateInternal(LookupEntity(Index));
	}

	/** 直接把 30-bit GenerationId 转成 int32 SerialNumber 暴露给上层。 */
	int32 FConcurrentEntityStorage::GetSerialNumber(int32 Index) const
	{
		return LookupEntity(Index).GenerationId;
	}

	/**
	 * 判断 Index 是否落在"已分配的 page 区间"内。Page 一旦分配就不会再被释放，
	 * 因此本判断对并发是稳定的（PageCount 单调递增）。
	 */
	bool FConcurrentEntityStorage::IsValidIndex(int32 Index) const
	{
		// Page Index is which page in the array of pages we need to access
		if (Index >= 0)
		{
			const uint32 PageIndex = static_cast<uint32>(Index) >> MaxEntitiesPerPageShift;
			return PageIndex < PageCount;
		}
		return false;
	}

	/**
	 * 句柄合法性：Index 在已分配范围内 + 槽 GenerationId == Handle.SerialNumber。
	 * 注：在并发情景下，IsValidHandle 与 IsValidHandle 之间可能有 Acquire/Release 让 Generation 跳变；
	 * 这是预期行为 —— 比对失败即识别该句柄已悬垂。
	 */
	bool FConcurrentEntityStorage::IsValidHandle(FMassEntityHandle EntityHandle) const
	{
		return IsValidIndex(EntityHandle.Index)
			&& LookupEntity(EntityHandle.Index).GetSerialNumber() == EntityHandle.SerialNumber;
	}

	/**
	 * 句柄合法 + 状态为 Created。注意：单次读取 EntityData 后做两次字段访问，
	 * 中间没有锁；如果有线程在此刻 Release 该实体，本函数仍可能返回 true 但下一帧失效。
	 * 这是"瞬时近似"语义，调用方需自行容错。
	 */
	bool FConcurrentEntityStorage::IsEntityActive(FMassEntityHandle EntityHandle) const
	{
		if (IsValidIndex(EntityHandle.Index))
		{
			const FEntityData& EntityData = LookupEntity(EntityHandle.Index);
			return EntityData.GetSerialNumber() == EntityHandle.SerialNumber
				&& GetEntityStateInternal(EntityData) == UE::Mass::IEntityStorageInterface::EEntityState::Created;
		}
		return false;
	}

	/**
	 * 估算总分配字节：page 数据 + 顶层指针表 + free-list 容量。
	 *
	 * 注意：无锁读取 PageCount/MaxEntitiesPerPage 等成员，跨线程时为瞬时近似。
	 *
	 * 实现细节中 MagPageCount 看起来像是"max page count"的拼写错误（MaxPageCount）。
	 * 不影响行为（只用于内存统计），但属于轻微的代码味道。
	 */
	SIZE_T FConcurrentEntityStorage::GetAllocatedSize() const
	{
		const SIZE_T EntityFreeListSizeBytes = EntityFreeIndexList.GetAllocatedSize();

		// Allocated size to pages
		// 已分配 page 的总字节数。
		const SIZE_T PageSizeBytes = ComputePageSize();
		const SIZE_T PageAllocatedSizeBytes = PageCount * PageSizeBytes;

		// Size of page pointer array
		// 顶层指针表（常驻，按上限算）。
		const uint32 MaxEntities = 1 << MaximumEntityCountShift;
		const uint32 MagPageCount = (MaxEntities / MaxEntitiesPerPage); // 注：变量名疑为 MaxPageCount 笔误
		const SIZE_T PagePointerArraySizeBytes = MagPageCount * sizeof(FEntityData**);
	
		return PageAllocatedSizeBytes + PagePointerArraySizeBytes + EntityFreeListSizeBytes;
	}

	bool FConcurrentEntityStorage::IsValid(int32 Index) const
	{
		return LookupEntity(Index).CurrentArchetype != nullptr;
	}

	/**
	 * 分配并发布一页内存到 EntityPages[PageCount]，把这一页的所有新 Index 推入 free-list。
	 *
	 * 锁约束（极重要）：
	 *   - 调用方必须持有 FreeListMutex（函数顶部 check）。
	 *   - 函数内部再获取 PageAllocateMutex。
	 *   - 由此固化锁顺序 Free → PageAllocate，永远不会反向。
	 *
	 * 内存初始化：
	 *   - 用 Memzero 而不是 placement-new 来初始化整页。这要求"全 0 字节"的 FEntityData
	 *     与默认构造的 FEntityData 字节一致，否则 TSharedPtr 内部会处于未定义状态。
	 *     该假设由 DebugAssumptionsSelfTest 在调试构建里守卫（注释中已被注释掉的 placement-new
	 *     循环留作"性能太差时的 fallback 备份"）。
	 *
	 * 哨兵实体：
	 *   - 第 0 页第 0 槽（即 Index=InvalidEntityIndex=0）特殊处理：placement-new 一个
	 *     默认 FEntityData，并把 bIsAllocated=1 + GenerationId++ —— 让它从一开始就处于
	 *     "Reserved"状态，永远不会被加入 free-list、永远不会发出 Index=0 的合法句柄。
	 *
	 * Free-list 入栈顺序：
	 *   - 从 NewEntityIndexEnd-1 向前 Push，让 Pop 时按"小 Index 优先"出栈。这样
	 *     第一次 AcquireOne 必定拿到 Index=1（紧邻哨兵），保持紧密布局，cache 友好。
	 *
	 * 返回 false 表示 OOM（FMemory::Malloc 失败）。
	 */
	bool FConcurrentEntityStorage::AddPage()
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/ConcurrentStorage"));
		check(FreeListMutex.IsLocked());
		// 二级锁：保护对 EntityPages[PageCount] 的写入与 PageCount++ 的发布。
		UE::TUniqueLock PageAllocateLock(PageAllocateMutex);

		// Allocate new page
		const uint32 NewPageIndex = PageCount;
		// 越界检查：(NewPageIndex+1) 个 page 的总实体上限不能超过 1<<MaximumEntityCountShift。
		checkf(((NewPageIndex + 1) << MaxEntitiesPerPageShift) < (1u << MaximumEntityCountShift), TEXT("Exhausted number of entities"));

		const uint64 PageSize = ComputePageSize();
		FEntityData* Page = static_cast<FEntityData*>(FMemory::Malloc(PageSize, alignof(FEntityData)));
		
		if (Page == nullptr)
		{
			// OOM：把锁顺地释放后返回失败，调用方负责回退。
			return false;
		}

		/*for (int32 Index = 0, End = MaxEntitiesPerPage; Index < End; ++Index)
		{
			new (Page + Index) FEntityData();
		}*/
		// Memzero 等价于"对每个 FEntityData 默认构造"—— 由 DebugAssumptionsSelfTest 守卫。
		FMemory::Memzero(Page, PageSize);

		EntityPages[PageCount] = Page;
		// PageCount 的写入"发布"了新页 —— 由于在 PageAllocateMutex 内 + 通常配合内存
		// 屏障（FTransactionallySafeMutex 释放时屏障），其它线程的 IsValidIndex 读取
		// 一旦看到新值就一定能看到 EntityPages[NewPageIndex] 的有效写入。
		++PageCount;

		// Somewhat tricksy thing here to be aware of
		// MassEntityManager expects the very first allocated entity to be at index 0
		static_assert(UE::Mass::Private::InvalidEntityIndex == 0, "Free Entity list algorithm depends on InvalidEntityIndex being 0");
		int32 NewEntityIndexStart;
		if (LIKELY(NewPageIndex != 0))
		{
			// 非首页：Index 区间 [PageStart, PageEnd)。
			NewEntityIndexStart = NewPageIndex << MaxEntitiesPerPageShift;
		}
		else
		{
			// 首页：把 Index=0 留作哨兵。
			NewEntityIndexStart = 1;
			// Allocate the 0th entity. It will always be the sentinel entity that InvalidEntityIndex points to.
			// 直接 placement-new 一个 FEntityData，确保（即使 Memzero 已经清过）布局明确，
			// 然后把它标记为已分配 + Generation=1，使得"Index=0"永远不会被复用。
			FEntityData* SentinelEntity = new (Page + UE::Mass::Private::InvalidEntityIndex) FEntityData();
			SentinelEntity->bIsAllocated = 1;
			++SentinelEntity->GenerationId;
		}
		
		const int32 NewEntityIndexEnd = (NewPageIndex + 1) << MaxEntitiesPerPageShift;

		// 一次性预留容量，避免 Push 多次扩容。
		EntityFreeIndexList.Reserve(NewEntityIndexEnd - NewEntityIndexStart);

		// Push free entities indices onto the stack backwards so new entities pop off in order
		// 倒序入栈 → Pop 顺序为递增。第一个 Acquire 拿到的就是 1, 2, 3, ...，紧凑布局。
		for (int32 NewEntityIndex = NewEntityIndexEnd - 1; NewEntityIndex >= NewEntityIndexStart; --NewEntityIndex)
		{
			// Setup the free list
			EntityFreeIndexList.Push(NewEntityIndex);
		}

		return true;
	}

	/**
	 * 单实体分配（线程安全）：
	 *   1) 持 FreeListMutex；
	 *   2) free-list 为空时调用 AddPage（注意：AddPage 假定锁已被持有）；
	 *   3) 弹出一个 Index，EntityCount++；
	 *   4) 锁外更新 GenerationId 与 bIsAllocated 并构造 Handle。
	 *
	 * 注意第 4 步在锁外 —— 这是有意的，因为 LookupEntity(Index) 返回的内存地址
	 * 在该 Index 已被本线程独占（free-list 已 pop），其它线程不会动这个槽。
	 *
	 * 关于 ++GenerationId：
	 *   - "Technically should not be necessary"是因为 AddPage 第一次创建的槽，
	 *     Memzero 后 GenerationId=0；按理直接发出 0 即可。但 FMassEntityHandle::IsValid()
	 *     约定 SerialNumber == 0 ⇒ "无效句柄"，因此第一次也必须自增到 1，避免误判。
	 *   - 之后每次 Release 还会再 ++GenerationId，所以 Acquire 后的 SerialNumber 永远 ≥ 1。
	 */
	FMassEntityHandle FConcurrentEntityStorage::AcquireOne()
	{
		int32 EntityIndex;
		{
			UE::TUniqueLock FreeListLock(FreeListMutex);
		
			if (UNLIKELY(EntityFreeIndexList.IsEmpty()))
			{
				AddPage();
			}

			EntityIndex = EntityFreeIndexList.Pop(EAllowShrinking::No);

			++EntityCount;
		}

		FEntityData& EntityData = LookupEntity(EntityIndex);
		// NOTE: Technically should not be necessary, however FEntityHandle::IsValid() makes the assumption
		// that SerialNum == 0 means an invalid Entity.  FMassArchetypeEntityCollection uses this assumption
		// and will fail IsValid() checks otherwise.
		// 必须自增至少一次，避免发出 Serial==0 的句柄被上层判定为无效。
		++EntityData.GenerationId;
		EntityData.bIsAllocated = 1;
		int32 SerialNumber = EntityData.GetSerialNumber();

		FMassEntityHandle Handle;
		Handle.SerialNumber = SerialNumber;
		Handle.Index = EntityIndex;
		return Handle;
	}

	/**
	 * 批量分配（线程安全）：循环式补充 free-list 直到拿到全部 N 个或 OOM。
	 *
	 * 每轮：
	 *   - 持 FreeListMutex；
	 *   - 若 free-list 空则 AddPage；如果 AddPage 失败（OOM）→ 跳出循环；
	 *   - 一次性 Pop min(剩余, free-list 长度) 个 Index，并在持锁时同步更新槽位。
	 *
	 * 注意：本实现是"在锁内"更新 GenerationId/bIsAllocated 的，与 AcquireOne 不同
	 * （AcquireOne 锁外更新）。两者都正确，因为只要本线程独占了 Index，更新就安全；
	 * 在锁内做有助于读者通过 PageCount/free-list 状态推断"槽已被分配"。
	 *
	 * 返回真实分配数；OOM 时可能少于请求数。
	 */
	int32 FConcurrentEntityStorage::Acquire(TArrayView<FMassEntityHandle> OutEntityHandles)
	{
		const int32 NumberToAdd = OutEntityHandles.Num();

		int32 CountAdded = 0;
		int32 CountLeft = NumberToAdd;
		int32 CurrentEntityHandleIndex = 0;

		while (CountLeft > 0)
		{
			UE::TUniqueLock FreeListLock(FreeListMutex);

			if (UNLIKELY(EntityFreeIndexList.IsEmpty()))
			{
				if (AddPage() == false)
				{
					// OOM；放弃后续请求并把 CountAdded 返回上层。
					break;
				}
			}

			const int32 CountToProcess = FMath::Min(CountLeft, EntityFreeIndexList.Num());

			for (int32 Iteration = 0; Iteration < CountToProcess; ++Iteration)
			{
				const int32 EntityIndex = EntityFreeIndexList.Pop(EAllowShrinking::No);

				FEntityData& EntityData = LookupEntity(EntityIndex);
				// NOTE: Technically should not be necessary, however FEntityHandle::IsValid() makes the assumption
				// that SerialNum == 0 means an invalid Entity.  FMassArchetypeEntityCollection uses this assumption
				// and will fail IsValid() checks otherwise.
				// 同 AcquireOne：必须递增 GenerationId 防止发出 Serial=0 的"伪无效"句柄。
				++EntityData.GenerationId;
				EntityData.bIsAllocated = 1;
				const int32 SerialNumber = EntityData.GetSerialNumber();

				OutEntityHandles[CurrentEntityHandleIndex++] = { EntityIndex, SerialNumber };
			}
			
			CountAdded += CountToProcess;
			EntityCount += CountToProcess;
			CountLeft -= CountToProcess;
		}

		return CountAdded;
	}

	/**
	 * 批量释放（带 Serial 校验，线程安全）：核心优化是"减少加锁次数"。
	 *
	 * 实现策略 —— 把"连续 Serial 匹配的句柄"打成一段 run，run 内的槽位清空在锁外完成；
	 * 只有把 Index 推入 EntityFreeIndexList 这一步才需要 FreeListMutex。
	 *
	 *   遍历 Handles：
	 *     - 匹配 → AllocatedRunLength++，立刻清空槽（generation++/allocated=0/archetype.Reset）；
	 *     - 不匹配 → 调用 FreeRunOfHandles：把上一段已清空的 run 一次性入 free-list（持锁）；
	 *                之后 BeginHandlesIndexToFree 跳过这条不匹配的句柄。
	 *
	 * 这样：当所有句柄都匹配（最常见情景）时，整个 Release 只持锁一次。
	 *
	 * ⚠ 潜在 race condition（疑似 bug，建议补到 ARCHITECTURE.md）：
	 *   - 槽位的 generation/allocated/archetype 三个字段是在锁外被改写的，
	 *     与"另一个线程的 Lookup（无锁读）"并发时，可能读到中间态：
	 *       (a) 已 generation++ 但 archetype 还没 Reset
	 *       (b) 已 archetype.Reset 但 allocated 还没 0
	 *     IsEntityActive 的两次字段读取可能返回旧 Serial + Created，下一刻就 stale 了。
	 *     调用方约定的"瞬时近似"语义可以容忍，但若用作严格的"是否安全访问 Archetype"，
	 *     则可能短暂读到已经 reset 的 TSharedPtr → 这是真正的并发隐患。
	 *   - 另一个实际隐患：本函数对 EntityData 的 generation++ 在锁外，
	 *     但同一 Index 可能正在被另一线程 Acquire（理论上不会，因为该 Index 还在
	 *     "已分配"集合中），所以严格说仍是单写多读，但对 generation/archetype 的
	 *     可见性顺序没有显式 release barrier，依赖锁后段的 Free/Page 锁释放来 publish。
	 *
	 * 返回真实释放数（Serial 校验通过的）。
	 */
	int32 FConcurrentEntityStorage::Release(TConstArrayView<FMassEntityHandle> Handles)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/ConcurrentStorage"));
		int32 DeallocateCount = 0;
	
		// run 模型：[BeginHandlesIndexToFree, BeginHandlesIndexToFree+AllocatedRunLength) 是当前已经清空槽位但
		// 还没回收 Index 到 free-list 的段。
		int32 BeginHandlesIndexToFree = 0;
		int32 AllocatedRunLength = 0;

		// Helper to add a range of handles to the EntityFreeIndexList
		// 把当前 run 一次性写入 free-list（持锁段）。空 run 时跳过加锁，避免无意义争用。
		auto FreeRunOfHandles = [this, &BeginHandlesIndexToFree, &AllocatedRunLength, Handles]()
		{
			if (AllocatedRunLength > 0) // Cheaper than taking the lock for each in case of runs of unallocated handles
			{
				UE::TUniqueLock FreeListLock(FreeListMutex);
				EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + AllocatedRunLength);
				for (int32 IndexToFree = BeginHandlesIndexToFree; IndexToFree < BeginHandlesIndexToFree + AllocatedRunLength; ++IndexToFree)
				{
					const FMassEntityHandle& HandleToFree = Handles[IndexToFree];
					EntityFreeIndexList.Add(HandleToFree.Index);
				}
			}
			BeginHandlesIndexToFree += (AllocatedRunLength + 1); // +1 to skip to next iteration
			AllocatedRunLength = 0;
		};
	
		for (int32 Index = 0, End = Handles.Num(); Index < End; ++Index)
		{
			const FMassEntityHandle& Handle = Handles[Index];
			FEntityData& EntityData = LookupEntity(Handle.Index);
			if (EntityData.GetSerialNumber() == Handle.SerialNumber)
			{
				++AllocatedRunLength;
			
				// 锁外更新槽位：generation 自增（让旧 Handle 立即失效）+ 标记 Free + 释放 archetype 引用。
				++EntityData.GenerationId;
				EntityData.bIsAllocated = 0;
				EntityData.CurrentArchetype.Reset();
			
				++DeallocateCount;
			}
			else
			{
				// Skip, this one isn't allocated
				// Return the last run to the free list
				// Ideally this code never runs but we cannot control what is passed into the Release() function
				// 出现 Serial 不匹配（句柄已悬垂）→ 中断当前 run，先把已清空的 run 入栈，再继续。
				FreeRunOfHandles();
			}
		}

		// Free any remaining handles
		// 处理收尾的最后一个 run。
		FreeRunOfHandles();

		{
			// 单独短临界段：维护 EntityCount。注意这段没有覆盖上面的"槽位写入"，
			// 所以 EntityCount 与槽位状态不是事务性一致 —— Num() 只是近似计数。
			UE::TUniqueLock FreeListLock(FreeListMutex);
			EntityCount -= DeallocateCount;
		}
	
		return DeallocateCount;
	}

	int32 FConcurrentEntityStorage::ReleaseOne(FMassEntityHandle Handle)
	{
		return Release(MakeArrayView(&Handle, 1));
	}

	/**
	 * 强制释放（跳过 Serial 校验）。调用方必须自行确保所有句柄都仍然合法。
	 *
	 * 实现比 Release 简单：所有句柄都按"已分配"对待，先在锁外清空所有槽位，
	 * 最后在一次锁内一次性把 Index 全部入栈 + 维护 EntityCount。
	 *
	 * 与 Release 一样，slot 字段更新在锁外 —— 同样存在"读者可能看到中间态"的语义。
	 */
	int32 FConcurrentEntityStorage::ForceRelease(TConstArrayView<FMassEntityHandle> Handles)
	{
		LLM_SCOPE_BYNAME(TEXT("Mass/ConcurrentStorage"));
		// ForceRelease assumes the caller knows all handles are allocated
		// no need to have complexity of tracking "runs" of handles 
		for (const FMassEntityHandle& Handle : Handles)
		{
			FEntityData& EntityData = LookupEntity(Handle.Index);

			++EntityData.GenerationId;
			EntityData.bIsAllocated = 0;
			EntityData.CurrentArchetype.Reset();
		}

		{
			UE::TUniqueLock FreeListLock(FreeListMutex);
			EntityFreeIndexList.Reserve(EntityFreeIndexList.Num() + Handles.Num());
			for (const FMassEntityHandle& Handle : Handles)
			{
				EntityFreeIndexList.Add(Handle.Index);
			}

			EntityCount -= Handles.Num();
		}

		return Handles.Num();
	}

	int32 FConcurrentEntityStorage::ForceReleaseOne(FMassEntityHandle Handle)
	{
		return ForceRelease(MakeArrayView(&Handle, 1));
	}

	/**
	 * 总槽位数 = MaxEntitiesPerPage * PageCount（包含 Free + Reserved + Created + 哨兵）。
	 *
	 * ⚠ 文档错误：接口注释说 Num() 返回的是"非 Free 实体数"，但本实现返回的是"已分配
	 *   的总槽位（含 Free 槽）"。语义与 SingleThreaded 实现保持一致（也是"槽位总数"），
	 *   但与接口文档不符。建议修文档。
	 *
	 * 另：尾部多余的 ";;"是无伤的冗余。
	 */
	int32 FConcurrentEntityStorage::Num() const
	{
		return MaxEntitiesPerPage * PageCount;;
	}

	int32 FConcurrentEntityStorage::ComputeFreeSize() const
	{
		return EntityFreeIndexList.Num();
	}

	/**
	 * Index → 槽引用。无锁；前提是 Index 落在已分配 page 范围内。
	 *
	 *   PageIdx     = (uint32)Index >> MaxEntitiesPerPageShift
	 *   InPageIndex = (uint32)Index - (PageIdx << MaxEntitiesPerPageShift)
	 *               = Index & (MaxEntitiesPerPage - 1)
	 *
	 * 内部 check 阻断负值传入（虽然 IsValidIndex 也会过滤，但这里防御性地再检查一次）。
	 *
	 * 线程安全性：EntityPages[PageIdx] 一旦被 AddPage 发布就不会再变；page 内存不会
	 * 移动；因此返回的引用在该 page 还活着（也就是本对象未析构）期间始终稳定。
	 */
	FConcurrentEntityStorage::FEntityData& FConcurrentEntityStorage::LookupEntity(int32 Index)
	{
		// PageIndex is which Page in the array of pages we need to access
		const uint32 PageIndex = static_cast<uint32>(Index) >> MaxEntitiesPerPageShift;

		// Convert the entity index into the index with respect to the page
		const uint32 EntityOffset = (PageIndex << MaxEntitiesPerPageShift);
		const uint32 InternalPageIndex = static_cast<uint32>(Index) - EntityOffset;

		check((Index >= 0) && (Index >= static_cast<int32>(EntityOffset))); // Check against negative values;

		// Pointer to start of page
		FEntityData* PageStart = EntityPages[PageIndex];
		FEntityData& EntityData = PageStart[InternalPageIndex];
		return EntityData;
	}

	const FConcurrentEntityStorage::FEntityData& FConcurrentEntityStorage::LookupEntity(int32 Index) const
	{
		// const 路径直接复用非 const 实现，避免代码重复（const_cast 安全：不修改 *this）。
		return const_cast<FConcurrentEntityStorage*>(this)->LookupEntity(Index);
	}

	/** 单页字节数 = sizeof(FEntityData) << log2(MaxEntitiesPerPage)。 */
	uint64 FConcurrentEntityStorage::ComputePageSize() const
	{
		return sizeof(FEntityData) << MaxEntitiesPerPageShift;
	}

#if WITH_MASSENTITY_DEBUG
	/**
	 * 自检：确认"FMemory::Memzero 一块原始内存"等价于"placement-new 默认构造的 FEntityData"。
	 *
	 * 为什么需要：AddPage 直接 Memzero 整页（性能远高于循环 placement-new）；前提是
	 *   - TSharedPtr 内部成员全 0 时是合法的"空指针"状态；
	 *   - GenerationId / bIsAllocated 默认值都是 0。
	 * 引擎升级或 TSharedPtr 内部布局重构时，本断言会立刻在 CI 中触发，提示调整 AddPage。
	 *
	 * 仅 WITH_MASSENTITY_DEBUG（非 Shipping/Test）下编译。
	 */
	bool FConcurrentEntityStorage::DebugAssumptionsSelfTest()
	{
		// future proofing in case FEntityData's or TSharedPtr's internals change and make MemZero-ing not produce 
		// the same results as default FEntityData's constructor
		FEntityData DefaultData;
		FEntityData ZeroedData;
		FMemory::Memzero(&ZeroedData, sizeof(FEntityData));

		if (DefaultData != ZeroedData)
		{
			UE_LOG(LogMass, Error, TEXT("%hs assumption about default FEntityData values is no longer true."), __FUNCTION__);
			return false;
		}

		return true;
	}
#endif // WITH_MASSENTITY_DEBUG

	//-----------------------------------------------------------------------------
	// FConcurrentEntityStorage::FEntityData
	//-----------------------------------------------------------------------------
	FConcurrentEntityStorage::FEntityData::~FEntityData() = default;

	/**
	 * 30-bit GenerationId → 32-bit SerialNumber。
	 * 由于 GenerationId 永远只占低 30 位，转换为 int32 后高位为 0，永远是非负数。
	 * SerialNumber == 0 表示"无效"（与 FMassEntityHandle::IsValid() 约定一致）；
	 * 因此 AcquireOne 必须保证至少 ++GenerationId 一次。
	 */
	int32 FConcurrentEntityStorage::FEntityData::GetSerialNumber() const
	{
		return static_cast<int32>(GenerationId);
	}

	/** 字节级相等比较；DebugAssumptionsSelfTest 用它判断默认构造 vs Memzero 是否等价。 */
	bool FConcurrentEntityStorage::FEntityData::operator==(const FEntityData& Other) const
	{
		return CurrentArchetype == Other.CurrentArchetype
			&& GenerationId == Other.GenerationId
			&& bIsAllocated == Other.bIsAllocated;
	}
}
