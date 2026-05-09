// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// =============================================================================
// MassEntityManagerStorage.h
// -----------------------------------------------------------------------------
// 本文件定义 MassEntity 框架的"实体 ID 分配器 / 存储后端"抽象。它解决一个核心问题：
//
//     给定一个 8 字节的 FMassEntityHandle = (int32 Index, int32 SerialNumber)，
//     如何在 O(1) 内查到该实体对应的 FMassArchetypeData* 与版本号？
//
// 与 Archetype 列式存储（每个 archetype 内部按 Chunk 装载组件）形成上下两层：
//   ┌────────────────────────────────────────────────────────────┐
//   │ Handle (Index, Serial) ── IEntityStorageInterface ──┐       │
//   │   ↓ Index                                          ↓       │
//   │   FEntityData { TSharedPtr<FMassArchetypeData>, Serial }    │
//   │                       │                                     │
//   │                       ↓                                     │
//   │           FMassArchetypeData → Chunks → 列式组件数据          │
//   └────────────────────────────────────────────────────────────┘
//
// 提供两种实现：
//   1) FSingleThreadedEntityStorage —— 主线程独占，TChunkedArray + free-list；
//      无锁，最快；任何分配/释放/读取都必须在 game thread 上执行。
//   2) FConcurrentEntityStorage —— 分页存储 + 互斥量；允许多个线程并发"预订
//      (Reserve)"和"释放 free 槽"，但 archetype 装配（Created）阶段仍受 Manager
//      上层保护。仅 WITH_MASS_CONCURRENT_RESERVE（编辑器或显式开启）时启用。
//
// SerialNumber 的设计目的：避免"Index 复用 → 悬垂引用"。任何 Release+Acquire
// 后 Serial 必然递增，旧 Handle 比对失败即识别为悬垂句柄。
// =============================================================================

#include "Async/TransactionallySafeMutex.h"
#include "Containers/ChunkedArray.h"
#include "Misc/TVariant.h"
#include "Templates/SharedPointer.h"
#include "MassProcessingTypes.h"

struct FMassArchetypeData;
struct FMassEntityHandle;

/**
 * Initialization parameters to configure MassEntityManager to reserve entities only single threaded
 * Supported in all build configurations
 *
 * 单线程后端的初始化参数（空标签结构）。所有构建配置都支持。
 * 通过 TVariant 选中此分支时，FMassEntityManager 会构造 FSingleThreadedEntityStorage。
 */
struct FMassEntityManager_InitParams_SingleThreaded {};

/**
 * Initialization parameters to configure MassEntityManager to concurrently reserve entities
 * Only supported in editor builds.
 *
 * Expected static memory requirement for array of Page pointers can be computed:
 * MaxPages = MaxEntityCount / MaxEntitiesPerPage
 * MemorySize = MaxPages * sizeof(Page**)
 *
 * For default values, expectation is 128kB
 *
 * 并发后端的初始化参数。仅在 WITH_MASS_CONCURRENT_RESERVE（默认 = WITH_EDITOR）下启用。
 *
 * 静态内存预估（页指针数组）：
 *     MaxPages    = MaxEntityCount / MaxEntitiesPerPage
 *     MemorySize  = MaxPages * sizeof(FEntityData**)
 * 默认值（10亿 / 65536 = 16384 页指针）≈ 128 KiB（这是常驻指针表，
 * 实际页面按需分配，未触达的页全部为 nullptr）。
 */
struct FMassEntityManager_InitParams_Concurrent
{
	/** 
	 * Maximum supported entities by the MassEntityManager
	 * Must be multiple of 2
	 *
	 * MassEntityManager 支持的最大实体数；必须是 2 的幂（FloorLog2 用作位移）。
	 * 默认 1 << 30 = 约 10 亿。注意 Index 是 int32，因此最大不能超过 2^31。
	 */
	uint32 MaxEntityCount = 1 << 30; // 1 billion

	/** 
	 * Number of entities per chunk
	 * Must be multiple of 2
	 *
	 * 每页（Page）的实体数；必须是 2 的幂（用位移与位与拆分 page-index 与 in-page-offset）。
	 * 默认 1 << 16 = 65536，对应一页 ≈ 65536 * sizeof(FEntityData) 字节。
	 */
	uint32 MaxEntitiesPerPage = 1 << 16; // 65536
};

/**
 * 通过 TVariant 聚合两套初始化参数；调用方在构造 FMassEntityManager 时
 * 二选一，决定使用单线程后端还是并发后端。FMassEntityManager 内部用
 * Visit() 模式分发到对应的实现。
 */
using FMassEntityManagerStorageInitParams = TVariant<FMassEntityManager_InitParams_SingleThreaded, FMassEntityManager_InitParams_Concurrent>;

namespace UE::Mass
{
	/**
	 * Interface that abstracts the storage system for Mass Entities in the EntityManager
	 * This may be temporary until the concurrent mechanism has been vetted for performance
	 *
	 * MassEntity 实体存储后端的纯虚接口。FMassEntityManager 通过指向此接口的指针完成所有
	 * Index 到 (Archetype*, Serial) 的查找，从而把"线程模型"完全隔离在具体实现里。
	 *
	 * 注释说明（线程约束）：
	 *   - SingleThreaded 实现：所有方法都假设调用方在主线程。无锁。
	 *   - Concurrent 实现：标注为"线程安全"的方法可在任意线程调用；其余仍要求外部协调。
	 *     具体见每个方法上方的注释。
	 *
	 * 注释中"may be temporary"表示本接口是过渡设计，等并发版本性能/正确性被验证后，
	 * 可能会被替换或合并。
	 */
	class IEntityStorageInterface
	{
	public:
		/**
		 * 实体槽的三种状态。状态机：
		 *
		 *           AcquireOne()              SetArchetype...
		 *   Free  ────────────────►  Reserved  ────────────────►  Created
		 *     ▲                         │                            │
		 *     │                         │                            │
		 *     └──────── Release(...) / ForceRelease(...) ─────────────┘
		 *
		 * - Free：该 Index 当前未持有任何实体，Serial=0（单线程实现），可被分配。
		 * - Reserved：已通过 Acquire 取得 Index 与 Serial，但还没绑定到 archetype。
		 *             典型场景是网络层"先预订实体 ID 再异步填充组件"。
		 * - Created：已通过 BuildEntity / SetArchetype 绑定到某个 archetype，
		 *           对应 Chunk 中也存有数据；这是"完整实体"的运行态。
		 */
		enum class EEntityState
		{
			/** Entity index refers to an entity that is free to be reserved or created */
			/** 该 Index 上的槽位是空闲的，可被复用。 */
			Free,
			/** Entity index refers to a reserved entity */
			/** 已分配 Index 与 SerialNumber，但 archetype 尚未关联（中间态）。 */
			Reserved,
			/** Entity index refers to an entity assigned to an archetype */
			/** 完整实体：archetype 已关联、组件数据已经放入 Chunk。 */
			Created
		};
		virtual ~IEntityStorageInterface() = default;

		// ----- Archetype 访问 ----------------------------------------------------
		// 仅在 Created 状态下返回非空。Index 必须 IsValidIndex 通过。
		// 单线程实现：必须主线程调用。
		// 并发实现：archetype 的写入（BuildEntity）由 Manager 串行化，但 GET 在
		//           Reserved/Created 状态下读取是允许的（目标槽位的内存稳定）。

		virtual FMassArchetypeData* GetArchetype(int32 Index) = 0;
		virtual const FMassArchetypeData* GetArchetype(int32 Index) const = 0;
		virtual TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) = 0;
		virtual const TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) const = 0;

		// SetArchetype 完成 Reserved → Created 的过渡。仅 Manager 内部在持有合适
		// 锁/串行化条件下调用（并发实现中，本操作不是无锁的）。
		virtual void SetArchetypeFromShared(int32 Index, TSharedPtr<FMassArchetypeData>& Archetype) = 0;
		virtual void SetArchetypeFromShared(int32 Index, const TSharedPtr<FMassArchetypeData>& Archetype) = 0;

		/**
		 * Returns true if the given entity at index is currently reserved
		 * False if free or assigned an archetype
		 *
		 * 返回 Index 对应槽位的状态（Free / Reserved / Created）。
		 *
		 * 注：注释中"Returns true if reserved"是历史遗留 —— 本函数实际返回的是枚举，
		 * 不是 bool。属于文档错误。
		 */
		virtual EEntityState GetEntityState(int32 Index) const = 0;

		/**
		 * 读取 Index 槽当前的 SerialNumber（即"代号 / 版本"）。Free 槽返回 0。
		 * 在并发实现中用于把 30-bit GenerationId 提升为对外可见的 int32。
		 */
		virtual int32 GetSerialNumber(int32 Index) const = 0;

		/** Checks if index can be used to access entity data */
		/** 判断 Index 是否落在已分配的存储范围内（不校验 Serial，不区分 Free/Reserved/Created）。 */
		virtual bool IsValidIndex(int32 Index) const = 0;

		/**
		 * Checks if the given handle is valid in the context od this storage, i.e. whether the
		 * index is valid and the serial number associated with it matches the handle's
		 *
		 * 判断 Handle (Index, Serial) 是否仍然指向同一个实体。流程：
		 *   1) Index 必须落在范围内；
		 *   2) 当前槽 Serial 必须等于 Handle.Serial。
		 * 这是防御"悬垂引用"的核心检查 —— 如果 Index 之前的实体已被 Release 然后又
		 * 被另一个实体 Acquire，Serial 会递增，旧 Handle 比对就会失败。
		 */
		virtual bool IsValidHandle(FMassEntityHandle EntityHandle) const = 0;

		/**
		 * 综合 IsValidHandle + 状态必须为 Created。即"句柄合法且实体已完整生成"。
		 */
		virtual bool IsEntityActive(FMassEntityHandle EntityHandle) const = 0;

		/**
		 * 返回当前后端动态分配的总字节数（用于内存统计 / Memreport）。
		 * 并发实现中无锁读取，瞬时值；用于 dump 即可。
		 */
		virtual SIZE_T GetAllocatedSize() const = 0;

		/** Checks if entity at Index is built */
		/** 等价于 GetEntityState(Index) == Created（archetype 非空）。 */
		virtual bool IsValid(int32 Index) const = 0;

		/**
		 * Produce a single entity handle
		 *
		 * 分配一个新实体（Free → Reserved 状态过渡）。
		 * 并发实现：线程安全。
		 * 单线程实现：必须主线程。
		 */
		virtual FMassEntityHandle AcquireOne() = 0;

		/**
		 * @return number of entities actually added
		 *
		 * 便利重载：内部分配 OutEntityHandles 数组并调用 Acquire(TArrayView)。
		 * 返回实际成功分配的数量；当存储满（OOM 或耗尽）时可能小于 Count。
		 */
		int32 Acquire(const int32 Count, TArray<FMassEntityHandle>& OutEntityHandles);

		/**
		 * 批量预订；将结果写入 OutEntityHandles。线程安全性同 AcquireOne。
		 * 并发实现尽量在一次锁持有内完成，减少争用。
		 */
		virtual int32 Acquire(TArrayView<FMassEntityHandle> OutEntityHandles) = 0;

		/**
		 * 释放一组 Handle（任何状态都可，serial 不匹配会被跳过）。
		 * 返回真实释放的数量（serial 校验通过的）。
		 * 并发实现：线程安全；内部按"连续匹配段"批量加入 free-list 以减少加锁次数。
		 */
		virtual int32 Release(TConstArrayView<FMassEntityHandle> Handles) = 0;
		virtual int32 ReleaseOne(FMassEntityHandle Handles) = 0;

		/**
		 * Bypasses Serial Number Check
		 * Only use if caller has ensured serial number matches or for debug purposes
		 *
		 * 跳过 SerialNumber 校验的强制释放。仅当调用方已确认 Serial 匹配或在 debug
		 * 路径下使用。误用会导致"释放别人正在持有的实体"，进而破坏迭代/引用。
		 */
		virtual int32 ForceRelease(TConstArrayView<FMassEntityHandle> Handles) = 0;
		virtual int32 ForceReleaseOne(FMassEntityHandle Handle) = 0;

		/**
		 * Returns the number of entities that are not free
		 * For debug purposes only. In multi-threaded environments, the result is going to be out of date
		 *
		 * 返回"非 Free"实体数量；仅用于调试与 dump。并发后端下数值是瞬时近似，
		 * 调用结束前可能已被其它线程改动。
		 */
		virtual int32 Num() const = 0;

		/**
		 * Returns the number of entities that are free
		 * For debug purposes only. In multi-threaded environments, the result is going to be out of date
		 *
		 * 返回 free-list 中槽位数；仅用于调试。同上，瞬时值。
		 */
		virtual int32 ComputeFreeSize() const = 0;
	};

	//-----------------------------------------------------------------------------
	// FSingleThreadedEntityStorage
	//-----------------------------------------------------------------------------
	/**
	 * This storage backend should be used when the user of MassEntityManager can guarantee
	 * that all entity management will be done on a single thread.
	 *
	 * 单线程实体存储后端。所有 Acquire / Release / 状态读取都必须在主线程完成；不加锁，
	 * 性能最优。这是 Mass 在游戏运行时（非编辑器）的默认后端。
	 *
	 * 内部数据结构：
	 *   1) Entities  : TChunkedArray<FEntityData>
	 *      - 选 chunked 而不是 TArray 的关键原因：TArray 在 reserve 不够时会 realloc
	 *        + memcpy，导致已有 FEntityData& 指针/引用失效；而帧内可能正在迭代一组实体、
	 *        外部 cache 也可能持有 archetype 指针。TChunkedArray 按固定大小的"块"线性追加，
	 *        块本身不搬动，只新分配下一个块，从而保证 push_back 不会让现有引用失效。
	 *      - 还可降低高水位时的内存峰值（相比 TArray 双倍扩容）。
	 *   2) EntityFreeIndexList : TArray<int32>
	 *      - 普通的 LIFO 栈作为"可复用 Index"的池子。Pop / Push 各 O(1)。
	 *      - 选数组栈而非链表：链表节点要额外指针 + cache miss；数组紧凑、易于
	 *        Reserve 一次容纳整批 Release。
	 *
	 * SerialNumber：单线程实现里所有 Acquire 共享一个全局自增计数器（NextSerialNumber）。
	 * 同一批 Acquire 的实体共用同一个 Serial（节省一次 fetch_add），不同批之间不会重复。
	 */
	class FSingleThreadedEntityStorage final : public IEntityStorageInterface
	{
	public:
		/** 初始化：内部分配 Index=0 的"哨兵"实体，使 InvalidEntityIndex(0) 永远不会被 Acquire 返回。 */
		void Initialize(const FMassEntityManager_InitParams_SingleThreaded&);
		// ----- IEntityStorageInterface 实现，语义同接口注释 ------------------------
		virtual FMassArchetypeData* GetArchetype(int32 Index) override;
		virtual const FMassArchetypeData* GetArchetype(int32 Index) const override;
		virtual TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) override;
		virtual const TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) const override;
		virtual void SetArchetypeFromShared(int32 Index, TSharedPtr<FMassArchetypeData>&) override;
		virtual void SetArchetypeFromShared(int32 Index, const TSharedPtr<FMassArchetypeData>&) override;
		virtual EEntityState GetEntityState(int32 Index) const override;
		virtual int32 GetSerialNumber(int32 Index) const override;
		virtual bool IsValidIndex(int32 Index) const override;
		virtual bool IsValidHandle(FMassEntityHandle EntityHandle) const override;
		virtual bool IsEntityActive(FMassEntityHandle EntityHandle) const override;
		virtual SIZE_T GetAllocatedSize() const override;
		virtual bool IsValid(int32 Index) const override;
		virtual FMassEntityHandle AcquireOne() override;
		using IEntityStorageInterface::Acquire;
		virtual int32 Acquire(TArrayView<FMassEntityHandle> OutEntityHandles) override;
		virtual int32 Release(TConstArrayView<FMassEntityHandle> Handles) override;
		virtual int32 ReleaseOne(FMassEntityHandle Handle) override;
		virtual int32 ForceRelease(TConstArrayView<FMassEntityHandle> Handles) override;
		virtual int32 ForceReleaseOne(FMassEntityHandle Handle) override;
		virtual int32 Num() const override;
		virtual int32 ComputeFreeSize() const override;

	private:
		/**
		 * 单条实体记录（占用一个槽位）。
		 * 当 SerialNumber == 0 表示 Free；非 0 + Archetype 为空表示 Reserved；非 0 + Archetype 非空表示 Created。
		 */
		struct FEntityData
		{
			/** 关联的 archetype（共享指针；archetype 自身在 Manager 的容器里持有强引用）。Reserved 状态下为空。 */
			TSharedPtr<FMassArchetypeData> CurrentArchetype;
			/** 该槽当前的版本号 / 代号；0 = Free。每次重新 Acquire 时被覆盖为新的全局 Serial。 */
			int32 SerialNumber = 0;

			~FEntityData();
			/** 把槽清回 Free 状态：丢弃 archetype 引用 + Serial 置 0。 */
			void Reset();
			/** 等价于"已经是 Created 状态"：SerialNumber!=0 && Archetype 有效。 */
			bool IsValid() const;
		};

		/**
		 * Serial 全局自增计数器。声明为 atomic 是为了"对 AutoRTFM 友好"：即使在 RTFM
		 * 事务里 fetch_add，也不会因事务回滚而把 Serial 还原（Serial 必须严格单调
		 * 递增，回滚会破坏唯一性）。所以下方 GenerateSerialNumber 也用 UE_AUTORTFM_ALWAYS_OPEN
		 * 修饰，告诉 AutoRTFM "这次自增不进事务、不可回滚"。
		 *
		 * 注意：本类号称单线程，那为什么用 atomic？—— 仅为 AutoRTFM 语义需要（"open"操作必须
		 * 通过 atomic 才能跨事务边界），并不代表本后端自身允许跨线程。
		 */
		std::atomic<int32> NextSerialNumber = 0;

		UE_AUTORTFM_ALWAYS_OPEN
		int32 GenerateSerialNumber()
		{
			// The serial number only needs to be unique; it doesn't need to be rolled back if an AutoRTFM transaction fails.
			// Serial 只需"全局唯一"，无须随事务回滚 → 用 fetch_add 最简单、且 always-open 跳过事务。
			return NextSerialNumber.fetch_add(1);
		}

		/** 实体数据存储；TChunkedArray 保证 push 不会使旧引用失效（详见类头注释）。 */
		TChunkedArray<FEntityData> Entities;
		/** 可复用的 Index 池（LIFO 栈）。Release 时入栈，Acquire 时优先出栈，无空位才扩 Entities。 */
		TArray<int32> EntityFreeIndexList;
	};

	//-----------------------------------------------------------------------------
	// FConcurrentEntityStorage
	//-----------------------------------------------------------------------------
	/**
	 * This storage backend allows for entities to be concurrently reserved. Reserved entities can also
	 * be concurrently freed.
	 * Creation of entities (i.e. assignment of an archetype and addition of data into chunks) cannot be done
	 * concurrently with this implementation.
	 *
	 * 并发实体存储后端。允许多个线程同时调用 Acquire/Release（从 Free 槽分配 Index 或归还
	 * Index），但"Created"路径（即 BuildEntity / SetArchetype / 把组件数据塞入 Chunk）依然
	 * 由 FMassEntityManager 上层串行化 —— 本类不解决"两个线程同时往同一个 archetype 的
	 * Chunk 里写"的并发问题。
	 *
	 * 内存布局（页式）：
	 *   EntityPages : FEntityData**           ← 大小固定 = MaxPages 个槽
	 *      ├─[0]── FEntityData[ MaxEntitiesPerPage ]   （第 0 页，按需分配）
	 *      ├─[1]── nullptr  （未触达，惰性分配）
	 *      ├─[2]── nullptr
	 *      ├─...
	 *      └─[N]── nullptr
	 *
	 *   Index → (PageIdx = Index >> MaxEntitiesPerPageShift,
	 *            InPageOffset = Index & (MaxEntitiesPerPage-1))
	 *
	 *   每个槽：FEntityData { TSharedPtr<Archetype>, GenerationId:30, bIsAllocated:1, padding }
	 *
	 * 关键设计点：
	 *   • 顶层指针表 EntityPages 在 Initialize 时一次性 malloc，一旦构造完成就只读，
	 *     除了 AddPage() 写入 EntityPages[PageCount++] —— 这一步在 PageAllocateMutex
	 *     保护下完成。读者通过 LookupEntity 直接索引，不加锁；当 Index 落在已分配的页
	 *     范围内时，对应的页指针 + 槽内存地址都是稳定的（页指针写入后不会移动，槽内存
	 *     不会 realloc）。这样 GetArchetype/GetSerialNumber 这类高频读取就是无锁的。
	 *   • free-list 由 FreeListMutex 保护，所有 Acquire/Release 都要拿这把锁。批量
	 *     Release 实现里把"连续匹配的句柄"合并为一段，再一次性加锁、批量入栈，把锁
	 *     竞争降到最低。
	 *   • SerialNumber 不再全局自增，而是"每个槽各自的 GenerationId"，30 位 → 同一槽
	 *     可被复用 ~10 亿次后才环绕。每次 Acquire 时 ++GenerationId，Release 时也再
	 *     ++GenerationId 一次，让在途请求拿到的旧 Serial 立刻失效。
	 *   • 锁顺序：先 FreeListMutex，再 PageAllocateMutex（AddPage 内部断言 FreeListMutex
	 *     已被持有，避免反向加锁导致死锁）。
	 */
	class FConcurrentEntityStorage final : public IEntityStorageInterface
	{
	public:

		/**
		 * 初始化：根据 MaxEntityCount / MaxEntitiesPerPage 计算页指针数组大小，一次性 malloc
		 * 并清零。注意此时还没有真正分配任何 page；第一次 AcquireOne 时由 AddPage 分配 page 0
		 * 并把 0 号 entity 设为哨兵（占用 InvalidEntityIndex 这个保留 Index）。
		 *
		 * 调用线程：构造期间，单线程。
		 */
		void Initialize(const FMassEntityManager_InitParams_Concurrent& InInitializationParams);

		/** 析构：释放所有已分配的页，再释放页指针表自身。 */
		virtual ~FConcurrentEntityStorage() override;

		// ----- IEntityStorageInterface 实现 -------------------------------------
		// 读类（GetArchetype / GetSerialNumber / IsValidHandle / IsEntityActive 等）：
		//   只要调用方持有的 Index 落在已分配 page 范围内，就是无锁读，可以与 Acquire/Release 并发。
		//   但要注意：当读取与 Release(Reserved→Free) 真正并发时，bIsAllocated/GenerationId
		//   的更新与 Archetype 的 reset 不是原子整体；调用方应用 SerialNumber 比对来识别"被释放"。
		// 写类（SetArchetype）：
		//   Manager 上层在串行段调用，本类不在写入处加锁。
		// Acquire/Release：
		//   由 FreeListMutex 保护，可在任意线程调用。
		virtual FMassArchetypeData* GetArchetype(int32 Index) override;
		virtual const FMassArchetypeData* GetArchetype(int32 Index) const override;
		virtual TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) override;
		virtual const TSharedPtr<FMassArchetypeData>& GetArchetypeAsShared(int32 Index) const override;
		virtual void SetArchetypeFromShared(int32 Index, TSharedPtr<FMassArchetypeData>& Archetype) override;
		virtual void SetArchetypeFromShared(int32 Index, const TSharedPtr<FMassArchetypeData>& Archetype) override;
		virtual EEntityState GetEntityState(int32 Index) const override;
		virtual int32 GetSerialNumber(int32 Index) const override;
		virtual bool IsValidIndex(int32 Index) const override;
		virtual bool IsValidHandle(FMassEntityHandle EntityHandle) const override;
		virtual bool IsEntityActive(FMassEntityHandle EntityHandle) const override;
		virtual SIZE_T GetAllocatedSize() const override;
		virtual bool IsValid(int32 Index) const override;
		virtual FMassEntityHandle AcquireOne() override;
		using IEntityStorageInterface::Acquire;
		virtual int32 Acquire(TArrayView<FMassEntityHandle> OutEntityHandles) override;
		virtual int32 Release(TConstArrayView<FMassEntityHandle> Handles) override;
		virtual int32 ReleaseOne(FMassEntityHandle Handle) override;
		virtual int32 ForceRelease(TConstArrayView<FMassEntityHandle> Handles) override;
		virtual int32 ForceReleaseOne(FMassEntityHandle Handle) override;
		virtual int32 Num() const override;
		virtual int32 ComputeFreeSize() const override;
#if WITH_MASSENTITY_DEBUG
		/** @return whether the assumptions are still valid */
		/**
		 * 自检：本类在 AddPage 时直接对一整页内存 Memzero，而不是 placement-new 每个 FEntityData。
		 * 这要求"全 0 的 FEntityData"与"默认构造的 FEntityData"是字节相同的，否则会出现
		 * 未初始化的 TSharedPtr 等灾难。本测试比较两种产物，确保 Epic 在某天改了
		 * FEntityData / TSharedPtr 内部布局后能立刻被 CI 抓到。
		 *
		 * 仅 WITH_MASSENTITY_DEBUG（开发/编辑器构建）下编译。
		 */
		MASSENTITY_API static bool DebugAssumptionsSelfTest();
#endif // WITH_MASSENTITY_DEBUG
	private:

		/**
		 * 槽位记录。布局采用位域，整体 = sizeof(TSharedPtr) + 4 字节，让单个槽紧凑。
		 *
		 * 状态判定（与 GetEntityStateInternal 对应）：
		 *
		 *   |  Archetype  | bIsAllocated |   状态     |
		 *   |  nullptr    |      0       |   Free     |
		 *   |  nullptr    |      1       |   Reserved |
		 *   |  !nullptr   |      1       |   Created  |
		 *   |  !nullptr   |      0       |   不可达    |
		 */
		struct FEntityData
		{
			/** GenerationId 占用的位数。32 - 1(状态位) = 31 但实现给到 30，留 1 位空白，这是历史/对齐选择。 */
			static constexpr int MaxGenerationBits = 30;

			/** 关联 archetype；Created 状态下非空。 */
			TSharedPtr<FMassArchetypeData> CurrentArchetype;
			/** Generation ID or version of the entity in this slot */
			/**
			 * 槽的代号 / 版本号；30 位（约 10 亿）。每次 Acquire/Release 都自增一次，
			 * 任何旧的 Handle.SerialNumber 都会比对失败 → 防止 Index 复用导致的悬垂引用。
			 *
			 * GetSerialNumber() 直接把 30-bit 提升为 int32 返回（对外接口要求 int32）。
			 * 由于第 31 位永远是 0，对外正负不会受影响。
			 */
			uint32 GenerationId : MaxGenerationBits = 0;
			/** 1 if the entity is NOT free */
			/** 状态位：0=Free / 已经被释放，1=Reserved 或 Created。配合 Archetype 判定状态。 */
			uint32 bIsAllocated : 1 = 0;

			~FEntityData();
			/** Converts EntityData state into a SerialNumber for public usage */
			/** 把 30-bit 的 GenerationId 投影到 int32 SerialNumber 公共接口（高位为 0）。 */
			int32 GetSerialNumber() const;

			/** 比较两个槽的字节等价性；DebugAssumptionsSelfTest 中使用。 */
			bool operator==(const FEntityData& Other) const;
		};

		/** 内部辅助：基于 Archetype 与 bIsAllocated 推断状态。 */
		EEntityState GetEntityStateInternal(const FEntityData& EntityData) const;

		/**
		 * Index → (PageIdx, InPageOffset) → EntityPages[PageIdx][InPageOffset]。
		 * 不加锁；前提是调用方保证 Index 落在已分配页范围内（IsValidIndex 通过）。
		 *
		 * 线程安全：与 Acquire/Release 并发时是安全的（页内存稳定），但读到的字段值是瞬时的。
		 */
		FEntityData& LookupEntity(int32 Index);
		const FEntityData& LookupEntity(int32 Index) const;

		/** Returns size of a page in bytes */
		/** 计算单页字节数 = sizeof(FEntityData) << MaxEntitiesPerPageShift。 */
		uint64 ComputePageSize() const;

		/**
		 * @return whether the operation was successful. Will return false when OOM
		 *
		 * 分配下一页并把它的全部新 Index 推入 free-list（页 0 内 Index=0 留作哨兵）。
		 *
		 * 线程约束（重要）：
		 *   - 调用方必须已经持有 FreeListMutex（函数内 check）。
		 *   - 函数内部还会再额外获取 PageAllocateMutex —— 锁顺序 Free → PageAllocate。
		 *   - 这样写的目的：让纯只读的 Lookup 能与"添加页"并发安全（顶层指针表的写入受
		 *     PageAllocateMutex 保护；读者只看已经发布的 PageCount 范围）。
		 */
		bool AddPage();

		/** Number of allocated Entities (only used for viewing in the debugger). */
		/**
		 * 已分配（非 Free）实体计数；仅作 debug 显示之用。Acquire/Release 在持锁段内
		 * 维护它，所以与 free-list 一致；但跨线程读取时仍是瞬时近似。
		 */
		uint32 EntityCount = 0;
		/** log2(MaxEntityCount)；用于上限检查。 */
		uint32 MaximumEntityCountShift = 0;
		/** 单页能容纳的实体数（必须 2 的幂）。 */
		uint32 MaxEntitiesPerPage = 0;
		/** log2(MaxEntitiesPerPage)；分页拆分时使用：PageIdx = Index >> Shift，InPage = Index & ((1<<Shift)-1)。 */
		uint32 MaxEntitiesPerPageShift = 0;
		/** 当前已分配的页数（即 EntityPages[0..PageCount-1] 非空）。 */
		uint32 PageCount = 0;
		/** ALWAYS acquire FreeListMutex before this one */
		/**
		 * 保护 EntityPages 数组中"新加一页"的写操作。锁顺序约束：必须先持有 FreeListMutex
		 * 才能拿到本锁，违反此顺序会死锁。
		 */
		UE::FTransactionallySafeMutex PageAllocateMutex;
		/** Pointer to array of pages */
		/** 页指针数组；大小固定 = MaxEntityCount / MaxEntitiesPerPage。未分配的槽为 nullptr。 */
		FEntityData** EntityPages = nullptr;

		/** 可复用 Index 池（LIFO 栈）。所有 Acquire/Release 都要先拿 FreeListMutex 才能访问。 */
		TArray<int32> EntityFreeIndexList;
		/** 保护 EntityFreeIndexList、EntityCount，以及 AddPage 调用的入口。 */
		UE::FTransactionallySafeMutex FreeListMutex;
	};

} // namespace UE::Mass
