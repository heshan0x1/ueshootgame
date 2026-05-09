// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// 【文件总览 / File Overview】
// MassEntityManager.h —— 整个 MassEntity 框架的"心脏"（the heart of MassEntity ECS）。
//
// FMassEntityManager 是 Mass ECS 的中心枢纽，所有子系统（Processor / Query / Observer /
// Subsystem / TypeManager / RelationManager / CommandBuffer 等等）几乎都会通过 EntityManager
// 来定位/操作 entity 与 archetype。它身兼数职：
//
//   1) 【Entity 仓库】持有 EntityStorage（FStorageType variant），负责
//      entity index 的 reserve / release，serial number 校验，entity → archetype 映射。
//   2) 【Archetype 注册中心】维护 AllArchetypes（按版本递增的全集），并通过两张哈希表
//      （FragmentHashToArchetypeMap、FragmentTypeToArchetypeMap）实现"composition 自动去重"
//      与"按 fragment 类型反查 archetype"，支撑 Query 高速匹配。
//   3) 【Shared Fragment 全局值池】通过 SharedFragmentsContainer / ConstSharedFragmentsContainer
//      按 CRC 去重存储 shared fragment 实例，跨 entity 复用同一份内存。
//   4) 【Observer 总线】内嵌 FMassObserverManager（不是独立子系统），负责在 add/remove
//      element / 实体创建/销毁时分发观察者通知，并通过 CreationContext / ObserverLock
//      支持批量操作时延迟通知。
//   5) 【Deferred Command 双缓冲】持有 DeferredCommandBuffers[2]，在 processor 运行期间
//      处理器代码不应直接调用同步 API（CHECK_SYNC_API 会断言失败），应通过 Defer() 拿到
//      command buffer，由 FlushCommands 在游戏线程串行 apply。两个 buffer 的设计是
//      为了支持 observer 在 flush 中再次 push command（不会回写到同一 buffer）。
//   6) 【ArchetypeGroup 管理】GroupNameToTypeIndex / GroupTypes 维护"按名字命名的逻辑分组"
//      （例如 LOD 桶、地区分桶），允许把"同一 composition + 不同 group"的 archetype 区分开。
//   7) 【TypeManager / RelationManager】通过 TSharedRef<FTypeManager> 持有类型注册中心，
//      并直接拥有 FRelationManager（关系图管理）。
//   8) 【FGCObject 集成】EntityManager 自身不是 UObject，但 shared/const shared fragment
//      内可能引用 UObject*，为避免被 GC 回收，FMassEntityManager 重写 AddReferencedObjects
//      参与引用收集；GetReferencerName 用于 GC 调试。
//
// 【线程模型】
//   - 大多数同步 API（CreateEntity / DestroyEntity / Add*ToEntity / SetEntityFragmentValues 等）
//     必须在 GameThread 调用，且必须在 IsProcessing()==false 时调用（CHECK_SYNC_API 强制检查）。
//   - Reserve 类 API 在 WITH_MASS_CONCURRENT_RESERVE 启用时可在工作线程调用（FConcurrentEntityStorage）。
//   - FlushCommands 仅 GameThread；processor 内部应使用 Context.Defer() 而非同步 API。
//
// 【常见使用方式】
//   - 推荐用 TSharedPtr/TSharedRef 持有（继承自 TSharedFromThis）。
//   - 自建实例必须先调用 Initialize()，然后 PostInitialize()，使用完毕后 Deinitialize()。
//   - FMassEntitySubsystem 内置一个全局共享实例，绝大多数游戏代码用那个就行。
//
// 【不应直接调用 EntityManager 的场景】
//   - 在 processor 的 Execute() 内：使用 ExecutionContext.Defer() 入队命令，由系统择机 flush。
//   - 在 observer 回调内（observer 已是 EntityManager 的内嵌子模块），同样推荐用 Defer。
// =============================================================================================

#pragma once

#include "UObject/GCObject.h"
#include "Containers/StaticArray.h"
#include "MassEntityTypes.h"
#include "MassProcessingTypes.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "MassEntityQuery.h"
#endif
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/StructUtilsTypes.h"
#endif
#include "MassObserverManager.h"
#include "Containers/MpscQueue.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "MassRequirementAccessDetector.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "Templates/FunctionFwd.h"
#include "MassEntityManagerStorage.h"
#include "MassArchetypeGroup.h"
#include "MassRelationManager.h"
#include "MassEntityRelations.h"

#if WITH_MASSENTITY_DEBUG
#include "Misc/EnumClassFlags.h"
#include "MassRequirementAccessDetector.h"
#else
struct FMassRequirementAccessDetector;
#endif

#define UE_API MASSENTITY_API

struct FInstancedStruct;
struct FMassEntityQuery;
struct FMassExecutionContext;
struct FMassArchetypeData;
struct FMassCommandBuffer;
struct FMassArchetypeEntityCollection;
class FOutputDevice;
struct FMassDebugger;
struct FMassFragmentRequirements;
enum class EMassFragmentAccess : uint8;
enum class EForkProcessRole : uint8;
namespace UE::Mass
{
	struct FEntityBuilder;
	struct FTypeManager;
	struct FTypeHandle;
	namespace Private
	{
		struct FEntityStorageInitializer;
	}
	namespace ObserverManager
	{
		struct FObserverLock;
		struct FCreationContext;
	}
}

// use REQUESTED_MASS_CONCURRENT_RESERVE=0 in your project's Build.cs file to disable concurrent storage
// NOTE that it will always be enabled in WITH_EDITOR since editor code requires it. 
// @see WITH_MASS_CONCURRENT_RESERVE
// 中文：通过项目 Build.cs 中设置 REQUESTED_MASS_CONCURRENT_RESERVE=0 可关闭并发 entity 存储。
//      但在编辑器（WITH_EDITOR）下始终强制开启——因为编辑器要支持随时新增/拖拽 entity。
#ifndef REQUESTED_MASS_CONCURRENT_RESERVE
#define REQUESTED_MASS_CONCURRENT_RESERVE 1
#endif

#define WITH_MASS_CONCURRENT_RESERVE (REQUESTED_MASS_CONCURRENT_RESERVE || WITH_EDITOR)

namespace UE::Mass
{
// 中文：FStorageType 是 EntityManager 与具体 entity 存储后端之间的"接口别名"。
//      - WITH_MASS_CONCURRENT_RESERVE=1：使用 IEntityStorageInterface 抽象接口，
//        实际实现可在单线程版本（FSingleThreadedEntityStorage）与并发版本
//        （FConcurrentEntityStorage）之间运行时切换。
//      - WITH_MASS_CONCURRENT_RESERVE=0：直接使用单线程实现，去掉虚函数调用开销。
//      EntityManager 内部 EntityStorage 是 TVariant<FEmpty, SingleThreaded, Concurrent>，
//      运行时根据 Initialize() 传入的 InitializationParams 选择。
#if WITH_MASS_CONCURRENT_RESERVE
	using FStorageType = IEntityStorageInterface;
#else
	using FStorageType = FSingleThreadedEntityStorage;
#endif //WITH_MASS_CONCURRENT_RESERVE
}

/** 
 * The type responsible for hosting Entities managing Archetypes.
 * Entities are stored as FEntityData entries in a chunked array. 
 * Each valid entity is assigned to an Archetype that stored fragments associated with a given entity at the moment. 
 * 
 * FMassEntityManager supplies API for entity creation (that can result in archetype creation) and entity manipulation.
 * Even though synchronized manipulation methods are available in most cases the entity operations are performed via a command 
 * buffer. The default command buffer can be obtained with a Defer() call. @see FMassCommandBuffer for more details.
 * 
 * FMassEntityManager are meant to be stored with a TSharedPtr or TSharedRef. Some of Mass API pass around 
 * FMassEntityManager& but programmers can always use AsShared() call to obtain a shared ref for a given manager instance 
 * (as supplied by deriving from TSharedFromThis<FMassEntityManager>).
 * IMPORTANT: if you create your own FMassEntityManager instance remember to call Initialize() before using it.
 *
 * 【中文-FMassEntityManager 总览】
 * Mass ECS 的核心实体管理器：
 * - 持有所有 entity 的索引（EntityStorage 内部按 chunked array 存储 FEntityData，
 *   每个 entry 包含 archetype 指针 + serial number + state(Reserved/Created)）。
 * - 持有所有 archetype（AllArchetypes 数组按创建顺序追加，索引 = ArchetypeDataVersion）。
 * - 实体的"属性"完全由 archetype 决定（同一 archetype 的所有 entity 拥有相同 fragment/tag 集合）。
 * - 提供两套 API：
 *     · 同步 API（CreateEntity / DestroyEntity / AddFragmentToEntity 等）—— 立即执行，
 *       但禁止在 processor 运行期间调用（CHECK_SYNC_API 会断言）；
 *     · 延迟 API（通过 Defer() 拿到 FMassCommandBuffer）—— processor 内部应该用这个。
 *
 * 【生命周期】
 *   构造（仅注册 Owner / 创建子模块）→ Initialize(InitParams)（创建 EntityStorage、CommandBuffer、
 *   注册类型）→ PostInitialize（注册内置类型、启动 ObserverManager）→ ... → Deinitialize → 析构。
 *
 * 【与 FMassEntitySubsystem 的关系】
 *   FMassEntitySubsystem 是 UWorldSubsystem，内部持有一个 FMassEntityManager 共享实例并代为
 *   Initialize/Deinitialize。游戏代码通常通过 UE::Mass::Utils::GetEntityManager(World) 拿到。
 */
struct FMassEntityManager : public TSharedFromThis<FMassEntityManager>, public FGCObject
{
	friend FMassEntityQuery;
	friend FMassDebugger;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNewArchetypeDelegate, const FMassArchetypeHandle&);

private:
	// Index 0 is reserved so we can treat that index as an invalid entity handle
	// 中文：第 0 号槽位永远不分配出去，这样 EntityHandle.Index==0 就可以当成"无效 handle"的哨兵值
	//      （配合 InvalidEntity 静态实例使用），无需额外的 bool 字段。
	constexpr static int32 NumReservedEntities = 1;
	
public:
	/**
	 * 中文：FScopedProcessing —— RAII 守卫，进入作用域时 ProcessingScopeCount+1，离开时 -1。
	 * 用法：processor 调度器在执行一波处理器之前 NewProcessingScope()，退出时计数自动减回。
	 *      之后任何同步 API 调用都会通过 CHECK_SYNC_API 检测 IsProcessing() 为真而断言失败，
	 *      强制开发者改用 CommandBuffer。
	 *      支持嵌套（atomic 计数器，多线程下也安全）。
	 */
	struct FScopedProcessing
	{
		explicit FScopedProcessing(std::atomic<int32>& InProcessingScopeCount) : ScopedProcessingCount(InProcessingScopeCount)
		{
			++ScopedProcessingCount;
		}
		~FScopedProcessing()
		{
			--ScopedProcessingCount;
		}
	private:
		std::atomic<int32>& ScopedProcessingCount;
	};
	using FStructInitializationCallback = TFunctionRef<void(void* Fragment, const UScriptStruct& FragmentType)>;

	// 中文：全局"无效实体"哨兵实例，等同于默认构造的 FMassEntityHandle()。
	const UE_API static FMassEntityHandle InvalidEntity;

	UE_API explicit FMassEntityManager(UObject* InOwner = nullptr);
	FMassEntityManager(const FMassEntityManager& Other) = delete;
	UE_API virtual ~FMassEntityManager();

	// FGCObject interface
	// 中文：FGCObject 集成。EntityManager 不是 UObject，但 SharedFragment / ConstSharedFragment
	//      内可能持有 UObject* 引用（FInstancedStruct 内的 UPROPERTY），需要在 GC 时主动汇报，
	//      否则这些 UObject 会被错误回收。AddReferencedObjects 会遍历 SharedFragmentsContainer
	//      和 ObserverManager（也通过 UScriptStruct 反射收集引用）。
	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMassEntityManager");
	}
	// End of FGCObject interface
	// 中文：用于 .Stat 的内存统计 —— 汇总 EntityStorage、archetype map、command buffer 各自占用。
	UE_API void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

	// Default to use single threaded implementation
	// 中文：无参 Initialize 走默认的单线程实现（FSingleThreadedEntityStorage）。
	//      这是绝大多数项目的默认选择。
	UE_API void Initialize();
	/**
	 * 中文：自定义初始化 —— 通过 InitializationParams 选择 entity 存储后端。
	 *      重复调用会被忽略并打日志（不会重新初始化）。
	 *      内部步骤：
	 *        1. Visit InitializationParams 创建 EntityStorage variant；
	 *        2. 创建 NumCommandBuffers 个 FMassCommandBuffer；
	 *        3. 监听 OnPostFork 委托（用于子进程派生后的清理）；
	 *        4. 遍历所有 UScriptStruct，给各类 BitSet（FMassFragmentBitSet 等）的 StructTracker
	 *           预先注册类型——保证不同 EntityManager 实例对同一类型分配同一 bit。
	 *        5. 标记 InitializationState=Initialized。
	 *      注意：这里不会注册 RelationManager 的 observer，那要等 PostInitialize 才做。
	 */
	UE_API void Initialize(const FMassEntityManagerStorageInitParams& InitializationParams);
	/**
	 * 中文：必须在 Initialize 之后、且所有相关 subsystem 都已就绪时调用。
	 *      负责：注册 TypeManager 内置类型；遍历已注册类型逐一调用 OnNewTypeRegistered，
	 *      把 Relation 类型的 observer hook 进 ObserverManager；初始化 ObserverManager。
	 *      为什么要分开两步：某些 processor 在自身 Initialize 期间会反过来访问 EntityManager，
	 *      所以 EntityManager 自己得先 Initialize 完，再统一 PostInitialize。
	 */
	UE_API void PostInitialize();
	/**
	 * 中文：清理函数。会清空 command buffer（不 flush，因为可能没有 owner 还活着了）、
	 *      释放 EntityStorage、关闭 ObserverManager，状态切换到 Deinitialized。
	 *      析构函数会自动调用，避免重复初始化。
	 */
	UE_API void Deinitialize();

	/** 
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain 
	 * both fragments and tags. 
	 *
	 * 中文：CreateArchetype 系列总览（共 5 个重载）
	 *  - 主入口：CreateArchetype(Composition, CreationParams) —— 真正干活的版本。
	 *  - 其它重载都是便利封装，先把入参整理成 FMassArchetypeCompositionDescriptor 再转发。
	 *
	 *  内部流程（核心算法）：
	 *    1. 计算 composition 哈希（包含 fragments + tags + chunk fragments + shared/const shared 的 bitset 哈希），
	 *       再叠加 GroupHandle 哈希（默认空）；
	 *    2. 在 FragmentHashToArchetypeMap 中查找该 hash 对应的 archetype 桶；
	 *    3. 桶内逐个 IsEquivalent 比较 composition + Groups —— 如果命中已存在的 archetype，直接复用；
	 *    4. 否则：++ArchetypeDataVersion；new FMassArchetypeData；初始化它的 chunk 布局；
	 *       追加到 AllArchetypes（保证 AllArchetypes.Num() == ArchetypeDataVersion）；
	 *       为每个 fragment 类型把 archetype 注册进 FragmentTypeToArchetypeMap；
	 *       Broadcast OnNewArchetypeEvent，Trace 一下。
	 *
	 *  也就是说 archetype 的"自动去重"是通过 hash + composition 比较实现的；同样 composition 的请求
	 *  永远只会得到同一个 archetype 实例。
	 *
	 *  这个重载允许 FragmentsAndTagsList 同时包含 fragment 和 tag（混合），代价是要逐个用 reflection
	 *  判断每个 type 是哪类，比纯 fragment list 慢。FragmentsAndTagsList 中含 chunk fragment 也支持。
	 */
	UE_API FMassArchetypeHandle CreateArchetype(TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/**
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain
	 * both fragments and tags. This version takes an original archetype and copies it layout, then appends any fragments and tags from the
	 * provided list if they're not already in the original archetype.
	 * 
	 * @param SourceArchetype The archetype where the composition will be copied from.
	 * @param FragmentsAndTagsList The list of fragments and tags to add to the copied composition.
	 */
	UE_API FMassArchetypeHandle CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList);
	
	/**
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain
	 * both fragments and tags. This version takes an original archetype and copies it layout, then appends any fragments and tags from the
	 * provided list if they're not already in the original archetype.
	 * 
	 * @param SourceArchetype The archetype where the composition will be copied from.
	 * @param FragmentsAndTagsList The list of fragments and tags to add to the copied composition.
	 * @param CreationParams Additional arguments used to create the new archetype.
	 */
	UE_API FMassArchetypeHandle CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, 
		const FMassArchetypeCreationParams& CreationParams);

	/**
	 * CreateArchetype from a composition descriptor and initial values
	 *
	 * @param Composition of fragment, tag and chunk fragment types
	 * @param CreationParams Parameters used during archetype construction
	 * @return a handle of a new archetype 
	 */
	UE_API FMassArchetypeHandle CreateArchetype(const FMassArchetypeCompositionDescriptor& Composition, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/**
	 *  Creates an archetype like SourceArchetype + InFragments.
	 *  @param SourceArchetype the archetype used to initially populate the list of fragments of the archetype being created.
	 *  @param InFragments list of unique fragments to add to fragments fetched from SourceArchetype. Note that
	 *   adding an empty list is not supported and doing so will result in failing a `check`
	 *  @return a handle of a new archetype
	 *  @note it's caller's responsibility to ensure that NewFragmentList is not empty and contains only fragment
	 *   types that SourceArchetype doesn't already have. If the caller cannot guarantee it use of AddFragment functions
	 *   family is recommended.
	 */
	UE_API FMassArchetypeHandle CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& InFragments);
	
	/** 
	 *  Creates an archetype like SourceArchetype + InFragments. 
	 *  @param SourceArchetype the archetype used to initially populate the list of fragments of the archetype being created. 
	 *  @param InFragments list of unique fragments to add to fragments fetched from SourceArchetype. Note that 
	 *   adding an empty list is not supported and doing so will result in failing a `check`
	 *  @param CreationParams Parameters used during archetype construction
	 *  @return a handle of a new archetype
	 *  @note it's caller's responsibility to ensure that NewFragmentList is not empty and contains only fragment
	 *   types that SourceArchetype doesn't already have. If the caller cannot guarantee it use of AddFragment functions
	 *   family is recommended.
	 */
	UE_API FMassArchetypeHandle CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& InFragments, 
		const FMassArchetypeCreationParams& CreationParams);

	/** 
	 * A helper function to be used when creating entities with shared fragments provided, or when adding shared fragments
	 * to existing entities
	 * @param ArchetypeHandle that's the assumed target archetype. But we'll be making sure its composition matches SharedFragmentsBitSet
	 * @param SharedFragmentBitSet indicates which shared fragments we want the target archetype to have. If ArchetypeHandle 
	 *	doesn't have these a new archetype will be created.
	 *
	 * 中文：当 caller 已经知道目标 archetype 的 fragments/tags，但还要额外补一组 shared fragments
	 *      时使用。如果原 archetype 已经包含这些 shared fragments，直接返回原 handle；
	 *      否则会创建一个 composition = 原 + shared/const-shared 的新 archetype。
	 *      用于 CreateEntity / BatchCreateEntities 内部，使 caller 不必手动构造 composition。
	 */
	UE_API FMassArchetypeHandle GetOrCreateSuitableArchetype(const FMassArchetypeHandle& ArchetypeHandle
		, const FMassSharedFragmentBitSet& SharedFragmentBitSet
		, const FMassConstSharedFragmentBitSet& ConstSharedFragmentBitSet
		, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/** Fetches the archetype for a given EntityHandle. If EntityHandle is not valid it will still return a handle, just with an invalid archetype */
	// 中文：安全版本 —— 先 IsEntityValid 校验，无效则返回空 handle。
	UE_API FMassArchetypeHandle GetArchetypeForEntity(FMassEntityHandle EntityHandle) const;
	/**
	 * Fetches the archetype for a given EntityHandle. Note that it's callers responsibility the given EntityHandle handle is valid.
	 * If you can't ensure that call GetArchetypeForEntity.
	 *
	 * 中文：Unsafe 版本 —— 跳过 IsEntityValid 校验，但仍 check IsValidIndex。
	 *      在 query 内部已经验证过 entity 有效性的热路径上使用，省一次 serial number 比对。
	 */
	UE_API FMassArchetypeHandle GetArchetypeForEntityUnsafe(FMassEntityHandle EntityHandle) const;

	/**
	 * Searches through all known archetypes and matches them to the provided requirements. All archetypes that pass the requirement check are returned.
	 * @param Requirements The set of fragments and tags that need to be on available in the request form on an archetype before it's added.
	 * @param OutValidArchetypes Archetypes that pass the requirements test are added here.
	 *
	 * 中文：FMassEntityQuery 的核心匹配函数。给定一组 Requirements（fragment/tag 的 All/Any/None
	 *      约束），遍历 AllArchetypes 找出所有满足约束的 archetype。Query 会缓存这个结果，
	 *      并通过 ArchetypeDataVersion 做增量更新（只检查上次缓存版本之后新增的 archetype）。
	 *      内部其实调用的是带 FromArchetypeDataVersion=0 的 protected 重载。
	 */
	UE_API void GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes) const;

	/** Method to iterate on all the fragment types of an archetype */
	// 中文：调试/工具用：枚举一个 archetype 的所有 fragment 类型。注意是 fragment 不含 tag/chunk fragment。
	static UE_API void ForEachArchetypeFragmentType(const FMassArchetypeHandle& ArchetypeHandle, TFunction< void(const UScriptStruct* /*FragmentType*/)> Function);

	/**
	 * Go through all archetypes and compact entities
	 * @param TimeAllowed to do entity compaction, once it reach that time it will stop and return
	 *
	 * 中文：实体压缩。Mass 用 chunked array 存 entity，删除会留下空位（"swap to last"风格），
	 *      长期运行后某些 chunk 利用率会下降。本函数遍历所有 archetype，在限定时间内把稀疏的
	 *      chunk 合并，回收空 chunk 内存。typically 由 EntitySubsystem tick 末尾调用。
	 *      时间预算用完会立即返回，下次 tick 接着压缩。
	 */
	UE_API void DoEntityCompaction(const double TimeAllowed);

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entity
	 * @return FMassEntityHandle id of the newly created entity
	 *
	 * 中文：【完整 CreateEntity 流程】
	 *  1. CHECK_SYNC_API：禁止在 processor 内调用；
	 *  2. ReserveEntity() —— EntityStorage 分配一个新的 entity index + serial number；
	 *  3. GetOrCreateSuitableArchetype 把 SharedFragmentValues 反映到 archetype（必要时新建）；
	 *  4. InternalBuildEntity：
	 *     a) GetEntityStorageInterface().SetArchetypeFromShared 把 entity → archetype 映射写回；
	 *     b) Archetype->AddEntity：在 archetype 的最后一个未满 chunk 添加该 entity 的 fragment 槽位；
	 *     c) ObserverManager.OnPostEntityCreated：根据 composition 触发观察者通知（如有）。
	 *  5. 返回完整 handle（Index + SerialNumber）。
	 *  失败：ArchetypeHandle 无效会 check 死，无效 SharedFragmentValues 不会被检查（caller 责任）。
	 */
	UE_API FMassEntityHandle CreateEntity(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param FragmentInstanceList is the fragments to create the entity from and initialize values
	 * @param SharedFragmentValues to be associated with the entity
	 * @return FMassEntityHandle id of the newly created entity
	 *
	 * 中文：与上面相比，这个版本根据 FragmentInstanceList 自动推导 archetype（无 tag/chunk fragment），
	 *      并把每个 instanced struct 的初始值写到 entity 的 fragment 上。
	 *      内部会用 CreationContext 包裹 InternalBuildEntity，避免在数据写入完成前就触发 observer。
	 */
	UE_API FMassEntityHandle CreateEntity(TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	using FEntityCreationContext = UE::Mass::ObserverManager::FCreationContext;

	/**
	 * The main use-case for this function is to create a blank FEntityCreationContext and hold on to it while creating 
	 * a bunch of entities (with multiple calls to BatchCreate* and/or BatchBuild*) and modifying them (with mutating batched API)
	 * while not causing multiple Observers to trigger. All the observers will be triggered at one go, once the FEntityCreationContext 
	 * instance gets destroyed.
	 * 
	 * !Important note: the "Creation Context" is a specialized wrapper for an "Observers Lock" (@see GetOrMakeObserversLock).
	 * As long as the creation context is alive all the operations will be assumed to affect the newly created entities.
	 * The consequence of that is operations performed on already existing entities won't be tracked, as long
	 * as the creation context is alive.
	 * Note that you can hold a FMassObserverManager::FObserverLock instance while the creation lock gets destroyed, the
	 * observers lock is a lower-level concept than the creation context.
	 * 
	 * @return the existing (if valid) or a newly created creation context
	 *
	 * 中文：【CreationContext 模式】
	 *      场景：要在一个 frame 内创建几百~几千个 entity 并立即修改它们的 fragment，
	 *      若每个 add/remove 都触发 observer，会有 N 倍开销。CreationContext 是 RAII 守卫：
	 *      - 在它存活期间，observer 不会被立即触发，所有变更会被记入 collection；
	 *      - 一旦最后一个 shared ref 析构，observer 才会以一次批量通知形式触发，
	 *        且只对"被该 context 跟踪的 entity"通知。
	 *      与 FObserverLock 的关系：CreationContext 是 ObserverLock 的超集 + 专门记录"新增"。
	 *      已经存在的 entity 在 context 期间被改动，不会归入 context 的 add 通知。
	 */
	UE_API TSharedRef<FEntityCreationContext> GetOrMakeCreationContext();

	/**
	 * Fetches the observers lock (as hosted by FMassObserverManager). If one is not currently active,
	 * one will be created. While the lock is active all the observers notifications are suspended, and
	 * will be sent out when FMassObserverManager::FObserverLock instance gets destroyed.
	 * Locking observers needs to be used when entities are being configured with multiple operations,
	 * and we want observers to be triggered only once all the operations are executed.
	 *
	 * Note that while the observers are locked we're unable to send "Remove" notifications, so once
	 * the lock is released and the observers get notified, the data being removed won't be available anymore
	 * (which is a difference in behavior as compared to removal notifications while the observers are not locked).
	 *
	 * 中文：比 CreationContext 更底层 —— 仅暂停 observer 通知（没有"新增 entity 集合"的语义）。
	 *      ⚠️ 注意 Lock 期间触发的 Remove 通知，等到 unlock 才发出，那时被删的 fragment 数据已经
	 *      不可访问，所以 Remove 观察者拿不到老数据 —— 这是与 unlocked 状态的语义差异。
	 *      如果观察者强依赖删除时的数据，避免在锁定状态下做 remove 操作。
	 */
	TSharedRef<UE::Mass::ObserverManager::FObserverLock> GetOrMakeObserversLock();

	/**
	 * A version of CreateEntity that's creating a number of entities (Count) in one go
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entities
	 * @param ReservedEntities a list of reserved entities that have not yet been assigned to an archetype.
	 * @return a creation context that will notify all the interested observers about newly created fragments once the context is released
	 *
	 * 中文：【批量 API 为何重要】
	 *  单 entity API 在 1000+ 实体时性能拙劣 —— 每个 entity 都要：哈希查 archetype、追加 chunk、
	 *  通知 observer。批量 API 把这些操作摊平：
	 *    · archetype 查找 1 次；
	 *    · chunk 一次性预分配（FMassArchetypeData::BatchAddEntities，可能跨多 chunk）；
	 *    · observer 通过 CreationContext 在最后一次性通知。
	 *  批量 API 的入参常见两种形态：
	 *    · TConstArrayView<FMassEntityHandle>（已 Reserve 的 handle 列表）；
	 *    · FMassArchetypeEntityCollectionWithPayload（已分类好的 collection + initial 值 payload）。
	 *
	 *  本重载用法：先 BatchReserveEntities 拿一堆 handle，再调本函数把它们 Build 进 archetype。
	 *  适合"先创建占位、后异步配置"的场景（例如对象池）。
	 */
	UE_API TSharedRef<FEntityCreationContext> BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities);
	inline TSharedRef<FEntityCreationContext> BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		TConstArrayView<FMassEntityHandle> OutEntities)
	{
		return BatchCreateReservedEntities(ArchetypeHandle, FMassArchetypeSharedFragmentValues(), OutEntities);
	}
	/**
	 * A version of CreateEntity that's creating a number of entities (Count) in one go
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entities
	 * @param Count number of entities to create
	 * @param InOutEntities the newly created entities are appended to given array, i.e. the pre-existing content of OutEntities won't be affected by the call
	 * @return a creation context that will notify all the interested observers about newly created fragments once the context is released
	 *
	 * 中文：一步到位的批量创建：内部先 BatchReserveEntities(Count) 再 InternalBatchCreateReservedEntities。
	 *      InOutEntities 用 append 语义（不清空原有内容），方便循环累积。
	 *      返回的 CreationContext 持有期间继续创建/修改也都会被合并通知。
	 */
	UE_API TSharedRef<FEntityCreationContext> BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const int32 Count, TArray<FMassEntityHandle>& InOutEntities);
	inline TSharedRef<FEntityCreationContext> BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle, const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
	{
		return BatchCreateEntities(ArchetypeHandle, FMassArchetypeSharedFragmentValues(), Count, InOutEntities);
	}

	/**
	 * Destroys a fully built entity, use ReleaseReservedEntity if entity was not yet built.
	 * @param EntityHandle identifying the entity to destroy
	 *
	 * 中文：【完整 DestroyEntity 流程】
	 *  1. CHECK_SYNC_API；2. CheckIfEntityIsActive（必须已 Built）；
	 *  3. ObserverManager.OnPreEntityDestroyed —— 在数据销毁前先通知（observer 还能读老数据）；
	 *  4. Archetype->RemoveEntity —— "swap to last"风格从 chunk 中移除；
	 *  5. InternalReleaseEntity → EntityStorage.ForceReleaseOne（不校验 serial number，因为前面已校验过）。
	 *  ⚠️ 区别：还未 Build 完的 reserved entity 应使用 ReleaseReservedEntity，本函数会 check fail。
	 *  ⚠️ 调用约束：必须在 GameThread + 非 Processing 状态。
	 */
	UE_API void DestroyEntity(FMassEntityHandle EntityHandle);

	/**
	 * Reserves an entity in the subsystem, the entity is still not ready to be used by the subsystem, need to call BuildEntity()
	 * @return FMassEntityHandle id of the reserved entity
	 *
	 * 中文：仅分配 entity index + serial number，不分配任何 fragment 内存（archetype 仍是空指针）。
	 *      之后必须通过 BuildEntity / BatchBuildEntities 把它绑定到某个 archetype；或者用
	 *      ReleaseReservedEntity 释放掉。
	 *      本函数线程安全：在 WITH_MASS_CONCURRENT_RESERVE 启用时，FConcurrentEntityStorage 允许
	 *      工作线程并发 reserve（用 lock-free MpscQueue 实现）。
	 */
	UE_API FMassEntityHandle ReserveEntity();

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param EntityHandle identifying the entity to build, which was retrieved with ReserveEntity() method
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entity
	 *
	 * 中文：把 ReserveEntity 拿到的 handle"激活"——分配到指定 archetype 的 chunk 中。
	 *      之后 IsEntityBuilt 才会返回 true。会触发 OnPostEntityCreated observer。
	 */
	UE_API void BuildEntity(FMassEntityHandle EntityHandle, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param EntityHandle identifying the entity to build, which was retrieved with ReserveEntity() method
	 * @param FragmentInstanceList is the fragments to create the entity from and initialize values
	 * @param SharedFragmentValues to be associated with the entity
	 *
	 * 中文：根据 fragment instance list 推导/创建 archetype 并 build。会用 CreationContext
	 *      包裹避免在 SetFragmentsData 完成前就触发 observer。
	 */
	UE_API void BuildEntity(FMassEntityHandle EntityHandle, TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/*
	 * Releases a previously reserved entity handle that was not yet built, otherwise call DestroyEntity
	 * @param EntityHandle to release
	 *
	 * 中文：释放仅 Reserved 但未 Build 的 entity，对应 ReserveEntity 的反操作。
	 *      若该 entity 已经 Build，会触发 check 失败——必须用 DestroyEntity。
	 */
	UE_API void ReleaseReservedEntity(FMassEntityHandle EntityHandle);

	/**
	 * Destroys all the entities in the provided array of entities. The function will also gracefully handle entities
	 * that have been reserved but not created yet.
	 * @note the function doesn't handle duplicates in InEntities.
	 * @param InEntities to destroy
	 *
	 * 中文：批量销毁。比 N 次 DestroyEntity 高效得多——逐个 entity 触发 OnPreEntityDestroyed
	 *      并从 archetype 移除，但 EntityStorage.Release 是一次批量调用。
	 *      ⚠️ 会断言 ObserverManager 没被锁住（见 GetOrMakeObserversLock 注释，锁住时
	 *      Remove 通知拿不到老数据）。
	 *      ⚠️ caller 需保证 InEntities 内无重复，否则 internal serial check 会跳过重复项但
	 *      逻辑上可能有问题。
	 */
	UE_API void BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities);

	/**
	 * Destroys all the entities provided via the Collection. The function will also gracefully handle entities
	 * that have been reserved but not created yet.
	 * @param Collection to destroy
	 *
	 * 中文：基于 archetype-grouped collection 的最高效销毁方式——可直接整 chunk 释放。
	 *      内部会检查 collection.IsUpToDate（archetype 版本一致）。
	 *      要求 Observer 未被锁定，原因同 BatchDestroyEntities。
	 */
	UE_API void BatchDestroyEntityChunks(const FMassArchetypeEntityCollection& Collection);
	UE_API void BatchDestroyEntityChunks(TConstArrayView<FMassArchetypeEntityCollection> Collections);

	/**
	 * Assigns all entities indicated by Collections to a given archetype group.
	 * Note that depending on their individual composition each entity can end up in a different archetype.
	 * @paramm GroupHandle indicates the target group. Passing an invalid group handle will get logged as warning and ignored.
	 *
	 * 中文：【ArchetypeGroup 概念】
	 *  ArchetypeGroup 是 Mass 在 5.6 引入的"逻辑分组"维度——同一 composition 但分到不同 group
	 *  的 entity 会落在不同的 archetype 上。这样可以做：
	 *    · LOD 桶：高 LOD vs 低 LOD entity 哪怕 composition 完全相同也分两 archetype，让 query 直接选；
	 *    · Region/Cluster：按地理分桶，便于空间局部性；
	 *    · Faction/Team：阵营分桶，做敌我筛选。
	 *  本函数把指定 entity 集合中的所有 entity 移到 GroupHandle 所代表的 group 对应的 archetype。
	 *  不同源 collection 由于 composition 不同，可能在加 group 后落到不同的目标 archetype。
	 *
	 *  GroupHandle 必须先通过 FindOrAddArchetypeGroupType 获取 group type，再构造具体的 group handle。
	 */
	UE_API void BatchGroupEntities(const UE::Mass::FArchetypeGroupHandle GroupHandle, TConstArrayView<FMassArchetypeEntityCollection> Collections);

	/**
	 * Assigns all entities indicated by InEntities to a given archetype group.
	 * Note that depending on their individual composition each entity can end up in a different archetype.
	 * @paramm GroupHandle indicates the target group. Passing an invalid group handle will get logged as warning and ignored.
	 *
	 * 中文：上一函数的便利封装——内部先把 InEntities 整理成 archetype-grouped collection 再分发。
	 */
	UE_API void BatchGroupEntities(const UE::Mass::FArchetypeGroupHandle GroupHandle, TConstArrayView<FMassEntityHandle> InEntities);

	/**
	 * Fetches FArchetypeGroupType instance (copy) associated with the given GroupName. A new group type
	 * is created if GroupName has not been used in the past.
	 *
	 * 中文：将 group name（如 "LOD"）映射到 group type index。同名再次调用返回同一 index。
	 *      内部 GroupNameToTypeIndex 是 TMap<FName,int32>。group type 是"分类"，
	 *      group handle = group type + group id（具体 LOD0 / LOD1 实例）。
	 */
	UE_API UE::Mass::FArchetypeGroupType FindOrAddArchetypeGroupType(const FName GroupName);

	// 中文：返回 archetype 当前所属的所有分组（FArchetypeGroups 是一组 group handle 的容器）。
	//      若 ArchetypeHandle 无效，返回静态空 FArchetypeGroups。
	UE_API const UE::Mass::FArchetypeGroups& GetGroupsForArchetype(const FMassArchetypeHandle& ArchetypeHandle) const;

	// 中文：【单 entity 修改 API 系列】（Add/Remove Fragment/Tag）
	//  这些函数的共同模式：
	//    1. CHECK_SYNC_API + CheckIfEntityIsActive；
	//    2. 计算新的 composition；
	//    3. CreateArchetype（去重，可能复用现有的）；
	//    4. MoveEntityToAnotherArchetype（拷贝公共 fragment，丢弃多余的）；
	//    5. EntityStorage.SetArchetypeFromShared 更新 entity → archetype 映射；
	//    6. 调用 ObserverManager.OnPostCompositionAdded / OnPreCompositionRemoved。
	//  注意：在 processor 内调用会断言失败 —— 改用 Context.Defer().AddFragment_RuntimeCheck(...)
	//        或对应的 PushCommand。
	UE_API void AddFragmentToEntity(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType);
	UE_API void AddFragmentToEntity(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType, const FStructInitializationCallback& Initializer);

	/** 
	 *  Ensures that only unique fragments are added. 
	 *  @note It's caller's responsibility to ensure EntityHandle's and FragmentList's validity. 
	 *
	 *  中文：内部用 BitSet 自动去重（实体已有的 fragment 跳过）；FragmentList 中的重复项也会去重。
	 */
	UE_API void AddFragmentListToEntity(FMassEntityHandle EntityHandle, TConstArrayView<const UScriptStruct*> FragmentList);

	// 中文：在添加 fragment 类型的同时立即拷贝初始值（FInstancedStruct 内部携带值）。
	UE_API void AddFragmentInstanceListToEntity(FMassEntityHandle EntityHandle, TConstArrayView<FInstancedStruct> FragmentInstanceList);
	UE_API void RemoveFragmentFromEntity(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType);
	UE_API void RemoveFragmentListFromEntity(FMassEntityHandle EntityHandle, TConstArrayView<const UScriptStruct*> FragmentList);

	UE_API void AddTagToEntity(FMassEntityHandle EntityHandle, const UScriptStruct* TagType);
	UE_API void RemoveTagFromEntity(FMassEntityHandle EntityHandle, const UScriptStruct* TagType);
	/**
	 * 中文：原子化"换 tag"——同时去 From、加 To，只触发一次 archetype 迁移。
	 *      用于 state machine：FStateA tag → FStateB tag。比"先 Remove 再 Add"快一倍。
	 *      ⚠️ 注意：本函数当前实现 *不* 触发 observer 通知（见 cpp 实现），与 AddTag+RemoveTag 不一致。
	 */
	UE_API void SwapTagsForEntity(FMassEntityHandle EntityHandle, const UScriptStruct* FromFragmentType, const UScriptStruct* ToFragmentType);

	/**
	 * Adds ElementType to the entities, treating it accordingly based on which element type it represents (i.e. Fragment or Tag).
	 * The function will assert on unhandled types.
	 */
	void AddElementToEntities(TConstArrayView<FMassEntityHandle> Entities, TNotNull<const UScriptStruct*> ElementType);

	/**
	 * Adds ElementType to the entity, treating it accordingly based on which element type it represents (i.e. Fragment or Tag).
	 * The function will assert on unhandled types.
	 */
	void AddElementToEntity(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType);

	/*
	 * Removes ElementType from the entities, treating it accordingly based on which element type it represents (i.e. Fragment or Tag).
	 * The function will assert on unhandled types.
	 */
	void RemoveElementFromEntities(TConstArrayView<FMassEntityHandle> Entities, TNotNull<const UScriptStruct*> ElementType);

	/*
	 * Removes ElementType from the entity, treating it accordingly based on which element type it represents (i.e. Fragment or Tag).
	 * The function will assert on unhandled types.
	 */
	void RemoveElementFromEntity(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType);

	/** @return whether Entity has an element of ElementType */
	bool DoesEntityHaveElement(FMassEntityHandle Entity, TNotNull<const UScriptStruct*> ElementType) const;

	/** 
	 * Adds a new const shared fragment to the given entity. Note that it only works if the given entity doesn't have
	 * a shared fragment of the given type. The function will give a soft "pass" if the entity has the shared fragment
	 * of the same value. Setting shared fragment value (i.e. changing) is not supported and the function will log
	 * a warning if that's attempted.
	 * @return whether the entity has the Fragment value assigned to it, regardless of its original state (i.e. the function will
	 *	return true also if the entity already had the same values associated with it)
	 */
	UE_API bool AddConstSharedFragmentToEntity(const FMassEntityHandle EntityHandle, const FConstSharedStruct& InConstSharedFragment);

	/**
	 * Removes a const shared fragment of the given type from the entity.
	 * Will do nothing if entity did not have the shared fragment.
	 * @return True if fragment removed from entity, false otherwise.
	 */
	UE_API bool RemoveConstSharedFragmentFromEntity(const FMassEntityHandle EntityHandle, const UScriptStruct& ConstSharedFragmentType);

	/**
	 * Adds a new shared fragment to the given entity. Note that it only works if the given entity doesn't have
	 * a shared fragment of the given type. The function will give a soft "pass" if the entity has the shared fragment
	 * of the same value. Setting shared fragment value (i.e. changing) is not supported and the function will log
	 * a warning if that's attempted.
	 * @return whether the entity has the Fragment value assigned to it, regardless of its original state (i.e. the function will
	 *	return true also if the entity already had the same values associated with it)
	 */
	UE_API bool AddSharedFragmentToEntity(const FMassEntityHandle EntityHandle, const FSharedStruct& InSharedFragment);

	/**
	 * Removes a shared fragment of the given type from the entity.
	 * Will do nothing if entity did not have the shared fragment.
	 * @return True if fragment removed from entity, false otherwise.
	 */
	UE_API bool RemoveSharedFragmentFromEntity(const FMassEntityHandle EntityHandle, const UScriptStruct& SharedFragmentType);

	/**
	 * Removes EntityHandle from any-and-all groups of given type - i.e. the entity will be moved to an archetype
	 * not in any of the groups of the given type.
	 */
	UE_API void RemoveEntityFromGroupType(FMassEntityHandle EntityHandle, UE::Mass::FArchetypeGroupType GroupType);

	/**
	 * @return the group handle of the specific group of type GroupType that the entity belongs to
	 */
	UE_API UE::Mass::FArchetypeGroupHandle GetGroupForEntity(FMassEntityHandle EntityHandle, UE::Mass::FArchetypeGroupType GroupType) const;

	/**
	 * Reserves Count number of entities and appends them to InOutEntities
	 * @return a view into InOutEntities containing only the freshly reserved entities
	 */
	UE_API TConstArrayView<FMassEntityHandle> BatchReserveEntities(const int32 Count, TArray<FMassEntityHandle>& InOutEntities);
	
	/**
	 * Reserves number of entities corresponding to number of entries in the provided array view InOutEntities.
	 * As a result InOutEntities gets filled with handles of reserved entities
	 * @return the number of entities reserved
	 */
	UE_API int32 BatchReserveEntities(TArrayView<FMassEntityHandle> InOutEntities);

	UE_API TSharedRef<FEntityCreationContext> BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload, const FMassFragmentBitSet& FragmentsAffected
		, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());
	/**
	 * 中文：【BatchBuildEntities 批量构建核心算法】
	 *  输入：EncodedEntitiesWithPayload —— 一组已 Reserve 但未 Build 的 entity handle（编码为
	 *  archetype-less subchunks），加上每个 entity 对应的 fragment 初始值 payload。
	 *  目的：把它们一次性绑定到一个目标 archetype 并写入初值，仅触发一次 observer 通知。
	 *
	 *  步骤：
	 *  1. CreateArchetype(Composition) 拿到/创建目标 archetype；
	 *  2. ConvertArchetypelessSubchunksIntoEntityHandles 把编码还原成 entity handle 数组；
	 *  3. 逐个 entity：补 SerialNumber（payload 编码省略了），并 SetArchetypeFromShared
	 *     更新 entity → archetype 映射。同时校验 EntityState == Reserved（不允许已 Created）；
	 *  4. ArchetypeData->BatchAddEntities：一次性在新 archetype 多个 chunk 中分配槽位，
	 *     输出 TargetArchetypeEntityRanges（entity 在新 archetype 中的位置）；
	 *  5. ArchetypeData->BatchSetFragmentValues：把 payload 数据写入相应 fragment 槽；
	 *  6. ObserverManager.GetOrMakeCreationContext 注册新 entity collection（要么新建 context，
	 *     要么追加到当前活跃 context），延迟到 context 析构时统一通知 observer。
	 *
	 *  与单 entity 路径的差别：
	 *    · archetype 查找 1 次 vs N 次；
	 *    · chunk 分配大块连续 vs 一次一槽位；
	 *    · observer 通知 1 次 vs N 次。
	 *
	 *  ⚠️ caller 必须保证所有 entity 都是 Reserved 状态（CreatedEntity 会 check fail）。
	 *  ⚠️ "EncodedEntitiesWithPayload" 编码的 entity collection 不带 archetype 信息，因此
	 *      caller 无需事先按 archetype 分组——本函数会把它们全部塞进同一目标 archetype。
	 */
	UE_API TSharedRef<FEntityCreationContext> BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload, const FMassArchetypeCompositionDescriptor& Composition
		, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());
	/**
	 * 中文：批量改 tag。对每个 collection 计算 NewTagComposition = Old + Add - Remove，
	 *      若发生变化则迁移到对应 similar archetype。
	 *      内部对 Remove 与 Add 分别触发 OnCompositionChanged，且只有在"该 tag 真有 observer
	 *      监听 Add"时才会输出 NewArchetypeEntityRanges——一种小优化，避免无观察者时白白分配。
	 */
	UE_API void BatchChangeTagsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassTagBitSet& TagsToAdd, const FMassTagBitSet& TagsToRemove);
	// 中文：批量改 fragment composition。逻辑与 BatchChangeTagsForEntities 类似，
	//      区别是若 collection.Archetype 为空（archetype-less reserved entity），会转去走
	//      BatchBuildEntities 路径——就是说 BatchAddElement 也能用于"刚 Reserve 还没 Build"的 entity。
	UE_API void BatchChangeFragmentCompositionForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassFragmentBitSet& FragmentsToAdd, const FMassFragmentBitSet& FragmentsToRemove);
	// 中文：批量加 fragment 类型 + 写入实例值。比"先 BatchChange + 再 BatchSet"少一次遍历。
	UE_API void BatchAddFragmentInstancesForEntities(TConstArrayView<FMassArchetypeEntityCollectionWithPayload> EntityCollections, const FMassFragmentBitSet& FragmentsAffected);
	/** 
	 * Adds a new const and non-const shared fragments to all entities provided via EntityCollections 
	 *
	 * 中文：批量添加共享 fragment 引用。每个 entity 仅持有"指向共享池中某条目"的引用，
	 *      不会复制数据；本函数仅修改 archetype 与每个 entity 的 SharedFragmentValues 集合。
	 */
	UE_API void BatchAddSharedFragmentsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassArchetypeSharedFragmentValues& AddedFragmentValues);

	/**
	 * Adds elements indicated by InOutDescriptor to the entity indicated by EntityHandle. The function also figures out which elements
	 * in InOutDescriptor are missing from the current composition of the given entity and then returns the resulting 
	 * delta via InOutDescriptor.
	 * If InOutDescriptor indicates shared fragments to be added the caller is required to provide matching values for the indicated
	 * shared fragment types, via AddedSharedFragmentValues.
	 *
	 * 中文：把 InOutDescriptor 里描述的"composition delta"应用到 entity，并把"实际新增了哪些"
	 *      回写到 InOutDescriptor。例：传入 {Fragment_A, Fragment_B}，若 entity 已经有 A，
	 *      返回时 InOutDescriptor 只剩 {B}（即真正发生变化的部分）。
	 *      ⚠️ 不支持新增 ChunkFragment（会 ensure fail）。
	 *      ⚠️ 若 InOutDescriptor 含 shared fragment，必须传 AddedSharedFragmentValues 提供具体值，
	 *         否则 ensure fail。
	 */
	UE_API void AddCompositionToEntity_GetDelta(FMassEntityHandle EntityHandle, FMassArchetypeCompositionDescriptor& InOutDescriptor, const FMassArchetypeSharedFragmentValues* AddedSharedFragmentValues = nullptr);
	// 中文：从 entity 移除 InDescriptor 描述的全部 element（fragment + tag + shared）。
	//      仅触发一次 OnPreCompositionRemoved。不支持移除 ChunkFragment。
	UE_API void RemoveCompositionFromEntity(FMassEntityHandle EntityHandle, const FMassArchetypeCompositionDescriptor& InDescriptor);

	// 中文：返回 archetype 当前的完整 composition 描述（fragments+tags+chunk+shared+const-shared 的 bitset 与类型列表）。
	UE_API const FMassArchetypeCompositionDescriptor& GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle) const;

	/** 
	 * Moves an entity over to a new archetype by copying over fragments common to both archetypes
	 * @param EntityHandle idicates the entity to move 
	 * @param NewArchetypeHandle the handle to the new archetype
	 * @param SharedFragmentValuesOverride if provided will override all given entity's shared fragment values
	 *
	 * 中文：【MoveEntityToAnotherArchetype 跨 archetype 迁移算法】
	 *  1. CompositionRemoved = OldArchetype.Composition - NewArchetype.Composition
	 *     → 触发 OnCompositionChanged(Remove)；observer 仍能读取被移除 fragment 的旧值。
	 *  2. Archetype 层面执行实际迁移（FMassArchetypeData::MoveEntityToAnotherArchetype）：
	 *     · 在新 archetype 找空位（最后一个未满 chunk）；
	 *     · 对两边都存在的 fragment：memcpy 旧 chunk → 新 chunk；
	 *     · 对仅旧 archetype 有的 fragment：调用其析构函数（FProperty::DestroyValue）；
	 *     · 对仅新 archetype 有的 fragment：用 fragment 的默认值初始化；
	 *     · 旧 chunk 的空位用"swap to last"补上；
	 *  3. EntityStorage.SetArchetypeFromShared 更新映射；
	 *  4. CompositionAdded = NewArchetype.Composition - OldArchetype.Composition
	 *     → 触发 OnCompositionChanged(Add)。
	 *  SharedFragmentValuesOverride 可强制覆盖 entity 的 shared fragment 引用集合（用于 AddSharedFragmentToEntity 等）。
	 */
	UE_API void MoveEntityToAnotherArchetype(FMassEntityHandle EntityHandle, FMassArchetypeHandle NewArchetypeHandle, const FMassArchetypeSharedFragmentValues* SharedFragmentValuesOverride = nullptr);

	/** 
	 *  Copies values from FragmentInstanceList over to target entity's fragment. Caller is responsible for ensuring that 
	 *  the given entity does have given fragments. Failing this assumption will cause a check-fail.
	 *  @param EntityHandle idicates the target entity
	 */
	UE_API void SetEntityFragmentValues(FMassEntityHandle EntityHandle, TArrayView<const FInstancedStruct> FragmentInstanceList);

	/** Copies values from FragmentInstanceList over to fragments of given entities collection. The caller is responsible 
	 *  for ensuring that the given entity archetype (FMassArchetypeEntityCollection .Archetype) does have given fragments. 
	 *  Failing this assumption will cause a check-fail. */
	UE_API void BatchSetEntityFragmentValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList);

	UE_API void BatchSetEntityFragmentValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList);

	/**
	 * @return whether the given handle represents a valid and built entity
	 *	 (i.e., the handle is valid and the entity represent has been constructed already)
	 */
	UE_API bool IsEntityActive(FMassEntityHandle EntityHandle) const;

	/**
	 * @return whether the given entity handle is valid, i.e. it points
	 *	 to a valid spot in the entity storage and the handle's serial number is up to date
	 */
	UE_API bool IsEntityValid(FMassEntityHandle EntityHandle) const;

	/** whether the entity handle represents an entity that has been fully built (expecting a valid EntityHandle) */
	UE_API bool IsEntityBuilt(FMassEntityHandle EntityHandle) const;

	/**
	 * @return whether the given EntityHandle is valid and the entity it represents is in `Reserved` state
	 *	 (i.e. it will also fail if the entity has already been `Created`)
	 */
	UE_API bool IsEntityReserved(FMassEntityHandle EntityHandle) const;

	/** Asserts that IsEntityValid */
	inline void CheckIfEntityIsValid(FMassEntityHandle EntityHandle) const
	{
		checkf(IsEntityValid(EntityHandle), TEXT("Invalid entity (ID: %d, SN:%d, %s)"), EntityHandle.Index, EntityHandle.SerialNumber,
			   (EntityHandle.Index == 0) ? TEXT("was never initialized") : TEXT("already destroyed"));
	}

	/** Asserts that IsEntityBuilt */
	inline void CheckIfEntityIsActive(FMassEntityHandle EntityHandle) const
	{
		checkf(IsEntityBuilt(EntityHandle), TEXT("Entity not yet created(ID: %d, SN:%d)"), EntityHandle.Index, EntityHandle.SerialNumber);
	}

	/**
	 * Generate valid, up-to-date entity handle for the entity at given index.
	 */
	UE_API FMassEntityHandle CreateEntityIndexHandle(const int32 EntityIndex) const;

	template<typename FragmentType>
	FragmentType& GetFragmentDataChecked(FMassEntityHandle EntityHandle) const
	{
		static_assert(UE::Mass::CFragment<FragmentType>, MASS_INVALID_FRAGMENT_MSG);
		return *((FragmentType*)InternalGetFragmentDataChecked(EntityHandle, FragmentType::StaticStruct()));
	}

	template<typename FragmentType>
	FragmentType* GetFragmentDataPtr(FMassEntityHandle EntityHandle) const
	{
		static_assert(UE::Mass::CFragment<FragmentType>, MASS_INVALID_FRAGMENT_MSG);
		return (FragmentType*)InternalGetFragmentDataPtr(EntityHandle, FragmentType::StaticStruct());
	}

	FStructView GetFragmentDataStruct(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType) const
	{
		checkf(UE::Mass::IsA<FMassFragment>(FragmentType)
			, TEXT("GetFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(FragmentType));
		return FStructView(FragmentType, static_cast<uint8*>(InternalGetFragmentDataPtr(EntityHandle, FragmentType)));
	}

	template<typename ConstSharedFragmentType>
	ConstSharedFragmentType* GetConstSharedFragmentDataPtr(FMassEntityHandle EntityHandle) const
	{
		static_assert(UE::Mass::CConstSharedFragment<ConstSharedFragmentType>, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		const FConstSharedStruct* ConstSharedStruct = InternalGetConstSharedFragmentPtr(EntityHandle, ConstSharedFragmentType::StaticStruct());
		return (ConstSharedFragmentType*)(ConstSharedStruct ? ConstSharedStruct->GetMemory() : nullptr);
	}

	template<typename ConstSharedFragmentType>
	ConstSharedFragmentType& GetConstSharedFragmentDataChecked(FMassEntityHandle EntityHandle) const
	{
		ConstSharedFragmentType* TypePtr = GetConstSharedFragmentDataPtr<ConstSharedFragmentType>(EntityHandle);
		check(TypePtr);
		return *TypePtr;
	}

	FConstStructView GetConstSharedFragmentDataStruct(FMassEntityHandle EntityHandle, const UScriptStruct* ConstSharedFragmentType) const
	{
		checkf(UE::Mass::IsA<FMassConstSharedFragment>(ConstSharedFragmentType)
			, TEXT("GetConstSharedFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(ConstSharedFragmentType));
		const FConstSharedStruct* ConstSharedStruct = InternalGetConstSharedFragmentPtr(EntityHandle, ConstSharedFragmentType);
		return ConstSharedStruct
			? FConstStructView(*ConstSharedStruct)
			: FConstStructView();
	}

	template<typename SharedFragmentType>
	TConstArrayView<FSharedStruct> GetSharedFragmentsOfType()
	{
		static_assert(UE::Mass::CSharedFragment<SharedFragmentType>
			, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");

		TArray<FSharedStruct>* InstancesOfType = SharedFragmentsContainer.Find(SharedFragmentType::StaticStruct());
		return InstancesOfType ? *InstancesOfType : TConstArrayView<FSharedStruct>();
	}

	template<typename SharedFragmentType>
	SharedFragmentType* GetSharedFragmentDataPtr(FMassEntityHandle EntityHandle) const
	{
		static_assert(UE::Mass::CSharedFragment<SharedFragmentType>
			, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		const FSharedStruct* FragmentPtr = InternalGetSharedFragmentPtr(EntityHandle, SharedFragmentType::StaticStruct());
		return (SharedFragmentType*)(FragmentPtr ? FragmentPtr->GetMemory() : nullptr);
	}

	template<typename SharedFragmentType>
	SharedFragmentType& GetSharedFragmentDataChecked(FMassEntityHandle EntityHandle) const
	{
		SharedFragmentType* TypePtr = GetSharedFragmentDataPtr<SharedFragmentType>(EntityHandle);
		check(TypePtr);
		return *TypePtr;
	}

	FConstStructView GetSharedFragmentDataStruct(FMassEntityHandle EntityHandle, const UScriptStruct* SharedFragmentType) const
	{
		checkf(UE::Mass::IsA<FMassSharedFragment>(SharedFragmentType)
			, TEXT("GetSharedFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(SharedFragmentType));
		const FSharedStruct* FragmentPtr = InternalGetSharedFragmentPtr(EntityHandle, SharedFragmentType);
		return FragmentPtr
			? FConstStructView(*FragmentPtr)
			: FConstStructView();
	}

	template<typename T>
	FConstStructView GetElementDataStruct(FMassEntityHandle EntityHandle, TNotNull<const UScriptStruct*> FragmentType) const
	{
		if constexpr (std::is_same_v<T, FMassFragment>)
		{
			return GetFragmentDataStruct(EntityHandle, FragmentType);
		}
		else if constexpr (std::is_same_v<T, FMassSharedFragment>)
		{
			return GetSharedFragmentDataStruct(EntityHandle, FragmentType);
		}
		else if constexpr (std::is_same_v<T, FMassConstSharedFragment>)
		{
			return GetConstSharedFragmentDataStruct(EntityHandle, FragmentType);
		}
		else
		{
			static_assert(UE::Mass::TAlwaysFalse<T>, "Unsupported element type passed to GetElementDataStruct");
			return {};
		}
	}

	// 中文：当前 archetype 集合的"版本号"。每创建一个新 archetype 就 +1。
	//      Query 缓存自身遍历到的最高版本，下次匹配只看 (CachedVersion .. CurrentVersion) 这段
	//      新增 archetype——这样几乎所有 frame 都不需要全量重扫，是 query 性能的关键。
	uint32 GetArchetypeDataVersion() const { return ArchetypeDataVersion; }

	/**
	 * Creates and initializes a FMassExecutionContext instance.
	 *
	 * 中文：构造一个绑定到当前 EntityManager + 当前 OpenedCommandBufferIndex 对应 buffer 的
	 *      FMassExecutionContext。Processor 通过这个 context 访问 entity 数据 + 入队延迟命令。
	 *      Processor 调度器会持有 context 直到该 phase 处理完。
	 */
	UE_API FMassExecutionContext CreateExecutionContext(const float DeltaSeconds);

	// 中文：开启一段"processor 处理中"的 scope。析构自动 -1。所有同步 API 都会断言
	//      IsProcessing()==false（CHECK_SYNC_API），强迫在处理中改用 CommandBuffer。
	FScopedProcessing NewProcessingScope() { return FScopedProcessing(ProcessingScopeCount); }

	/** 
	 * Indicates whether there are processors out there performing operations on this instance of MassEntityManager. 
	 * Used to ensure that mutating operations (like entity destruction) are not performed while processors are running, 
	 * which rely on the assumption that the data layout doesn't change during calculations. 
	 *
	 * 中文：true 表示当前至少有一个处理器正在跑（FScopedProcessing 计数 > 0）。
	 *      此时 archetype/chunk 的内存布局必须保持不变，所以同步 API 全部禁用。
	 */
	bool IsProcessing() const { return ProcessingScopeCount > 0; }

	// 中文：拿到"当前正在收集命令"的 buffer。Processor 内部应通过 ExecutionContext.Defer()
	//      间接调用，外部代码若需要也能直接 Defer().PushCommand(...)。
	//      ⚠️ buffer 内部检查调用线程：默认仅 GameThread 可 push（除非显式 ForceUpdateCurrentThreadID）。
	FMassCommandBuffer& Defer() const { return *DeferredCommandBuffers[OpenedCommandBufferIndex].Get(); }
	/** 
	 * @param InCommandBuffer if not set then the default command buffer will be flushed. If set and there's already 
	 *		a command buffer being flushed (be it the main one or a previously requested one) then this command buffer 
	 *		will be queued itself.
	 *
	 * 中文：将 InCommandBuffer 中的命令合入主 buffer 并 flush。
	 *      若已有 flush 进行中（bCommandBufferFlushingInProgress），InCommandBuffer
	 *      会被 AppendCommands 转入主 buffer 排队。
	 *      限制：仅 GameThread；IsProcessing()==false。
	 */
	UE_API void FlushCommands(const TSharedPtr<FMassCommandBuffer>& InCommandBuffer);

	/**
	 * 中文：【FlushCommands 双缓冲流转算法】
	 *  目标：observer 在 flush 期间也可能 push 新命令，但不能写回到正在 flush 的 buffer。
	 *  实现：NumCommandBuffers=2 双缓冲，状态机如下：
	 *    1. 进入 flush，标记 bCommandBufferFlushingInProgress=true（防止重入）；
	 *    2. CommandBufferIndexToFlush = OpenedCommandBufferIndex；
	 *    3. OpenedCommandBufferIndex = (OpenedCommandBufferIndex+1) % 2  —— 提前切换，
	 *       这样在 flush 期间任何 Defer() 拿到的都是新 buffer，不会污染正在 flush 的 buffer；
	 *    4. DeferredCommandBuffers[CommandBufferIndexToFlush]->Flush(*this) 真正执行命令；
	 *    5. flush 过程中 observer 可能往新 buffer push 更多命令；
	 *    6. flush 完成后检查新 buffer 是否非空，若是则继续循环（最多 5 次，超过则 Error 日志）；
	 *    7. ON_SCOPE_EXIT 复位 bCommandBufferFlushingInProgress。
	 *
	 *  上限 5 次是为了防止"observer 反复 push 导致永不收敛"的死循环。
	 */
	UE_API void FlushCommands();

	/** 
	 * Depending on the current state of Manager's command buffer the function will either move all the commands out of 
	 * InOutCommandBuffer into the main command buffer or append it to the list of command buffers waiting to be flushed.
	 * @note as a consequence of the call InOutCommandBuffer can get its contents emptied due some of the underlying code using Move semantics
	 *
	 * 中文：把外部 buffer 的命令并入主 buffer（move 语义，原 buffer 变空）。常用于
	 *      processor 自己持有的 thread-local command buffer 在 phase 结束时合并回主 buffer。
	 */
	UE_API void AppendCommands(const TSharedPtr<FMassCommandBuffer>& InOutCommandBuffer);

	template<typename T>
	UE_DEPRECATED(5.5, "This method will no longer be exposed. Use GetOrCreateConstSharedFragment instead.")
	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, const T& Fragment)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		return ConstSharedFragmentsContainer.FindOrAdd(Hash, T::StaticStruct(), reinterpret_cast<const uint8*>(&Fragment));
	}

private:
	template<typename T>
	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, const T& Fragment)
	{
		static_assert(UE::Mass::CSharedFragment<T>, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		return SharedFragmentsContainer.FindOrAdd(Hash, T::StaticStruct(), reinterpret_cast<const uint8*>(&Fragment));
	}

	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
	{
		return ConstSharedFragmentsContainer.FindOrAdd(Hash, InScriptStruct, InStructMemory);
	}

	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
	{
		return SharedFragmentsContainer.FindOrAdd(Hash, InScriptStruct, InStructMemory);
	}

	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		return ConstSharedFragmentsContainer.FindOrAdd<T>(Hash, Forward<TArgs>(InArgs)...);
	}

public:

#if WITH_EDITOR || WITH_MASSENTITY_DEBUG
	template<typename CallableT>
	void ForEachArchetype(int32 BeginRange, int32 EndRange, const CallableT& Callable) const
	{
		if (EndRange > AllArchetypes.Num())
		{
			EndRange = AllArchetypes.Num();
		}
		for (int32 Cursor = BeginRange; Cursor < EndRange; ++Cursor)
		{
			FMassArchetypeHandle Handle(AllArchetypes[Cursor]);
			Callable(*this, Handle);
		}
	}
#endif

	//-----------------------------------------------------------------------------
	// Shared fragments
	//-----------------------------------------------------------------------------
	// 中文：【SharedFragment 共享值池机制】
	//  Shared fragment 与 const shared fragment 的核心思路：
	//    - 多个 entity 可能需要相同一份"配置数据"（比如同种 AI 的 stats、同一渲染材质的参数）。
	//      若每个 entity 各存一份，会浪费内存且修改时不一致。
	//    - SharedFragment 把数据放进 EntityManager 的共享池（SharedFragmentsContainer），
	//      entity 只持有"指向池中某条目"的索引/引用。
	//    - 通过 CRC 自动去重：相同字段值的两次 GetOrCreateSharedFragment 调用返回同一引用。
	//
	//  GetOrCreateSharedFragment 的多种重载支持：
	//    1) 已有实例 (T&) → 用 GetStructInstanceCrc32 算 hash；
	//    2) 提供构造参数 (TArgs...) → 临时构造再 hash；
	//    3) 提供 (UScriptStruct&, uint8* memory) → 通用 reflection 路径；
	//    4) 提供 HashingHelperStruct + 构造参数 → 用辅助结构（不同字段子集）算 hash，例如多次创建
	//       但仅看其中一个 ID 字段是否相同。
	//
	//  Const 版与非 Const 版完全对称，区别仅在于 entity 是 const 借阅 vs 可写借阅。
	//  实际数据池由 ConstSharedFragmentsContainer / SharedFragmentsContainer 维护
	//  （TSharedFragmentsContainer 模板，见 private 区）。
	template<typename T, typename... TArgs>
	UE_DEPRECATED(5.5, "This method will no longer be exposed. Use GetOrCreateSharedFragment instead.")
	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CSharedFragment<T>, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		return SharedFragmentsContainer.FindOrAdd<T>(Hash, Forward<TArgs>(InArgs)...);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when an instance of the desired const shared fragment type is available and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntConstSharedFragment Fragment;
	 *	Fragment.Value = 123;
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment(Fragment);
	 *
	 * @params Fragment Instance of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const T& Fragment)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(FConstStructView::Make(Fragment));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, Fragment);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when an instance of the desired shared fragment type is available and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntSharedFragment Fragment;
	 *	Fragment.Value = 123;
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment(Fragment);
	 *
	 * @params Fragment Instance of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T>
	const FSharedStruct& GetOrCreateSharedFragment(const T& Fragment)
	{
		static_assert(UE::Mass::CSharedFragment<T>,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(FConstStructView::Make(Fragment));
		return GetOrCreateSharedFragmentByHash(Hash, Fragment);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when values can be provided as constructor arguments for the desired const shared fragment type and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
 	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragment(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment<FIntConstSharedFragment>(123);
	 *
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		T Struct(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(FConstStructView::Make(Struct));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, MoveTemp(Struct));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when values can be provided as constructor arguments for the desired shared fragment type and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
 	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntSharedFragment(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment<FIntSharedFragment>(123);
	 *
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FSharedStruct& GetOrCreateSharedFragment(TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CSharedFragment<T>,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		T Struct(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(FConstStructView::Make(Struct));
		return GetOrCreateSharedFragmentByHash(Hash, MoveTemp(Struct));
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when the reflection data and the memory of an instance of the desired const shared fragment type
	 * is available and that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 * e.g.,
	 * FSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment(*StructView.GetScriptStruct(), StructView.GetMemory());
	 *
	 * @params InScriptStruct Reflection data structure associated to the desired fragment type
	 * @params InStructMemory Actual data of the desired fragment type 
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const UScriptStruct& InScriptStruct, const uint8* InStructMemory)
	{
		checkf(InScriptStruct.IsChildOf(TBaseStructure<FMassConstSharedFragment>::Get()),
			TEXT("Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types."));
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(InScriptStruct, InStructMemory);
		return GetOrCreateConstSharedFragmentByHash(Hash, &InScriptStruct, InStructMemory);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when the reflection data and the memory of an instance of the desired shared fragment type
	 * is available and that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 * e.g.,
	 * FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment(*StructView.GetScriptStruct(), StructView.GetMemory());
	 *
	 * @params InScriptStruct Reflection data structure associated to the desired fragment type
	 * @params InStructMemory Actual data of the desired fragment type 
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	const FSharedStruct& GetOrCreateSharedFragment(const UScriptStruct& InScriptStruct, const uint8* InStructMemory)
	{
		checkf(InScriptStruct.IsChildOf(TBaseStructure<FMassSharedFragment>::Get()),
			TEXT("Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types."));
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(InScriptStruct, InStructMemory);
		return GetOrCreateSharedFragmentByHash(Hash, &InScriptStruct, InStructMemory);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when a different struct should be used to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 * and values can be provided as constructor arguments for the desired const shared fragment type
	 *	e.g.,
	 *
	 *	USTRUCT()
	 *	struct FIntConstSharedFragmentParams
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragmentParams(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragment(const FIntConstSharedFragmentParams& InParams) : Value(InParams.Value) {}
	 *
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntConstSharedFragmentParams Params(123);
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment<FIntConstSharedFragment>(FConstStructView::Make(Params), Params);
	 *
	 * @params HashingHelperStruct Struct view passed to UE::StructUtils::GetStructInstanceCrc32 to compute the CRC
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const FConstStructView HashingHelperStruct, TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CConstSharedFragment<T>,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		T Fragment(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(HashingHelperStruct);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, MoveTemp(Fragment));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when a different struct should be used to compute a CRC (i.e., UE::StructUtils::GetStructInstanceCrc32)
	 * and values can be provided as constructor arguments for the desired shared fragment type
	 *	e.g.,
	 *
	 *	USTRUCT()
	 *	struct FIntSharedFragmentParams
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FInSharedFragmentParams(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntSharedFragment(const FIntConstSharedFragmentParams& InParams) : Value(InParams.Value) {}
	 *
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntSharedFragmentParams Params(123);
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment<FIntSharedFragment>(FConstStructView::Make(Params), Params);
	 *
	 * @params HashingHelperStruct Struct view passed to UE::StructUtils::GetStructInstanceCrc32 to compute the CRC
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FSharedStruct& GetOrCreateSharedFragment(const FConstStructView HashingHelperStruct, TArgs&&... InArgs)
	{
		static_assert(UE::Mass::CSharedFragment<T>,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		T Fragment(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructInstanceCrc32(HashingHelperStruct);
		return GetOrCreateSharedFragmentByHash(Hash, MoveTemp(Fragment));
	}

	template<UE::Mass::CSharedFragment T>
	void ForEachSharedFragment(TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction);

	template<UE::Mass::CSharedFragment T>
	void ForEachSharedFragmentConditional(TFunctionRef< bool(T& /*SharedFragment*/) > ConditionFunction, TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction);

	template<UE::Mass::CConstSharedFragment T>
	void ForEachConstSharedFragment(TFunctionRef< void(const T& /*ConstSharedFragment*/) > ExecuteFunction);

	template<UE::Mass::CConstSharedFragment T>
	void ForEachConstSharedFragmentConditional(TFunctionRef< bool(const T& /*ConstSharedFragment*/) > ConditionFunction, TFunctionRef< void(const T& /*ConstSharedFragment*/) > ExecuteFunction);

	//-----------------------------------------------------------------------------
	// Relations
	//-----------------------------------------------------------------------------
	// 中文：关系管理子模块（M12 引入）。Relation 是 entity 之间的有向连接，
	//      由 RelationManager 持有 entity-pair 的图结构，并能注册 observer 在
	//      Subject/Object 销毁时自动断开关系。
	UE::Mass::FRelationManager& GetRelationManager();

	// 中文：模板入口——按编译期 Relation 类型 T 转成 TypeHandle 后转发到通用版本。
	template<UE::Mass::CRelation T>
	bool BatchCreateRelations(TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects);
	// 中文：批量建立 Subject[i] → Object[i] 的关系实例。Subjects.Num() 应等于 Objects.Num()
	//      （或 1：N/N:1 的特殊语义见 RelationManager 文档）。返回 true 当至少创建了一条关系。
	UE_API bool BatchCreateRelations(const UE::Mass::FTypeHandle RelationTypeHandle, TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects);

	//-----------------------------------------------------------------------------
	// Type management
	//-----------------------------------------------------------------------------
	// 中文：当 TypeManager 注册一个新类型时被调用。如果是 Relation 类型，转发给
	//      OnRelationTypeRegistered 让 RelationManager 注册 observer。
	void OnNewTypeRegistered(UE::Mass::FTypeHandle RegisteredTypeHandle);

protected:
	void OnRelationTypeRegistered(UE::Mass::FTypeHandle RegisteredTypeHandle, const UE::Mass::FRelationTypeTraits& RelationTypeTraits);

public:
	//-----------------------------------------------------------------------------
	// Entity Builder
	//-----------------------------------------------------------------------------
	// 中文：返回一个 FEntityBuilder——便于以"流式 API"逐步添加 fragment/tag/shared，
	//      最后 .Commit() 创建 entity。比直接 CreateEntity 更易读。
	[[nodiscard]] UE_API UE::Mass::FEntityBuilder MakeEntityBuilder();

	//-----------------------------------------------------------------------------
	// Sub-Managers
	//-----------------------------------------------------------------------------
	// 中文：子管理器访问点。这些子模块都是 EntityManager 内嵌（非独立 subsystem），
	//      生命周期与 EntityManager 一致。
	const UE::Mass::FTypeManager& GetTypeManager() const;
	UE::Mass::FTypeManager& GetTypeManager();

	FMassObserverManager& GetObserverManager() { return ObserverManager; }

	// 中文：archetype 创建事件。Query 系统订阅它做缓存增量更新（与 ArchetypeDataVersion 配合）。
	FOnNewArchetypeDelegate& GetOnNewArchetypeEvent() { return OnNewArchetypeEvent; }
	/** 
	 * Fetches the world associated with the Owner. 
	 * @note that it's ok for a given EntityManager to not have an owner or the owner not being part of a UWorld, depending on the use case
	 *
	 * 中文：Owner 通常是 FMassEntitySubsystem，但允许 EntityManager 独立存在（无 owner，
	 *      或 owner 不在 World 内，例如离线工具/测试用例）。
	 */
	UWorld* GetWorld() const { return Owner.IsValid() ? Owner->GetWorld() : nullptr; }
	UObject* GetOwner() const { return Owner.Get(); }

	// 中文：true 当存在活跃的 CreationContext。可用作"现在是不是在批量创建期间"的判定，
	//      处理器可据此决定是否合并 add/remove 通知。
	bool IsDuringEntityCreation() const;

	UE_API void SetDebugName(const FString& NewDebugGame);
#if WITH_MASSENTITY_DEBUG
	UE_API void DebugPrintArchetypes(FOutputDevice& Ar, const bool bIncludeEmpty = true) const;
	UE_API void DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty = true) const;
	UE_API void DebugGetArchetypeFragmentTypes(const FMassArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutFragmentList) const;
	UE_API int32 DebugGetArchetypeEntitiesCount(const FMassArchetypeHandle& Archetype) const;
	UE_API int32 DebugGetArchetypeEntitiesCountPerChunk(const FMassArchetypeHandle& Archetype) const;
	UE_API int32 DebugGetEntityCount() const;
	UE_API int32 DebugGetArchetypesCount() const;
	UE_API void DebugRemoveAllEntities();
	UE_API void DebugForceArchetypeDataVersionBump();
	UE_API void DebugGetArchetypeStrings(const FMassArchetypeHandle& Archetype, TArray<FName>& OutFragmentNames, TArray<FName>& OutTagNames);
	UE_DEPRECATED(5.7, "Using CreateEntityIndexHandle instead.")
	UE_API FMassEntityHandle DebugGetEntityIndexHandle(const int32 EntityIndex) const;
	UE_API const FString& DebugGetName() const;

	enum class EDebugFeatures
	{
		None,
		TraceProcessors = 1 << 0, // Used to track information about processors such as their name.
		All = TraceProcessors
	};

	UE_API void DebugEnableDebugFeature(EDebugFeatures Features);
	UE_API void DebugDisableDebugFeature(EDebugFeatures Features);
	UE_API bool DebugHasAllDebugFeatures(EDebugFeatures Features) const;

	UE_API FMassRequirementAccessDetector& GetRequirementAccessDetector();

	// For use by the friend MassDebugger
	UE_API UE::Mass::FStorageType& DebugGetEntityStorageInterface();
	// For use by the friend MassDebugger
	UE_API const UE::Mass::FStorageType& DebugGetEntityStorageInterface() const;

	UE_API bool DebugHasCommandsToFlush() const;
#endif // WITH_MASSENTITY_DEBUG

protected:
	/** Called on the child process upon process's forking
	 *
	 * 中文：进程派生（fork）后子进程的清理回调。OS-level fork 会复制全部状态，但 thread ID
	 *      / 线程局部存储已失效。本函数：
	 *      - ForceUpdateCurrentThreadID：让 command buffer 接受新线程的 push；
	 *      - 若 buffer 指针丢失（不应发生），重建 buffer 实例；
	 *      - 让 ObserverManager 也做对应清理（清失效的 lock/context）。
	 *      仅在 EForkProcessRole::Child 上执行。
	 */
	UE_API void OnPostFork(EForkProcessRole Role);

	// 中文：增量版的 GetMatchingArchetypes。FromArchetypeDataVersion 之前的 archetype 跳过——
	//      Query 用这个做"只检查上次缓存以来的新增"。
	UE_API void GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes, const uint32 FromArchetypeDataVersion) const;
	
	/** 
	 * A "similar" archetype is an archetype exactly the same as SourceArchetype except for one composition aspect 
	 * like Fragments or "Tags" 
	 *
	 * 中文：基于现有 archetype 派生一个仅"某个维度"不同的新 archetype（其它维度照搬）。
	 *      用于 AddTag/RemoveTag/AddFragment 等需要"原 composition + delta"的场景，
	 *      避免 caller 手动构造完整 composition descriptor。内部仍走 hash 去重。
	 */
	UE_API FMassArchetypeHandle InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags);
	UE_API FMassArchetypeHandle InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& OverrideFragments);
	UE_API FMassArchetypeHandle InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const UE::Mass::FArchetypeGroups& GroupsOverride);
	UE_API FMassArchetypeHandle InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition, const UE::Mass::FArchetypeGroups& GroupsOverride);

	// 中文：把 fragment+tag 混合 list 拆解到 InOutComposition 的对应 bitset 中。
	//      非 fragment/tag/chunk fragment 的类型会打 warning 并忽略（不会 crash）。
	UE_API void InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(FMassArchetypeCompositionDescriptor& InOutComposition,
		TConstArrayView<const UScriptStruct*> FragmentsAndTagsList) const;

private:
	// 中文：InternalBuildEntity —— Build/CreateEntity 的共同私有路径：
	//   SetArchetypeFromShared → archetype.AddEntity → Trace → ObserverManager.OnPostEntityCreated。
	void InternalBuildEntity(FMassEntityHandle EntityHandle, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues);
	// 中文：ForceReleaseOne，不校验 serial（caller 已保证）。
	void InternalReleaseEntity(FMassEntityHandle EntityHandle);

	/** 
	 *  Adds fragments in FragmentList to the entity indicated by EntityHandle. Only the unique fragments will be added.
	 *  @return Bitset for the added fragments (might be empty or a subset of `InFragments` depending on the current archetype fragments)
	 *
	 *  中文：与 Unchecked 版的差别——会过滤掉 entity 已有的 fragment 并打 log，返回真正新增的 bitset
	 *      （供 caller 用来传递给 observer notify）。
	 */
	FMassFragmentBitSet InternalAddFragmentListToEntityChecked(FMassEntityHandle EntityHandle, const FMassFragmentBitSet& InFragments);

	/** 
	 *  Similar to InternalAddFragmentListToEntity but expects NewFragmentList not overlapping with current entity's
	 *  fragment list. It's callers responsibility to ensure that's true. Failing this will cause a `check` fail.
	 *
	 *  中文：要求 InFragments 与 entity 已有 fragment 完全不重叠（caller 责任）。
	 *      内部直接 CreateArchetype + MoveEntityToAnotherArchetype，不做去重。
	 */
	void InternalAddFragmentListToEntity(FMassEntityHandle EntityHandle, const FMassFragmentBitSet& InFragments);
	/** Note that it's the caller's responsibility to ensure `FragmentType` is a kind of FMassFragment */
	UE_API void* InternalGetFragmentDataChecked(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType) const;
	/** Note that it's the caller's responsibility to ensure `FragmentType` is a kind of FMassFragment */
	UE_API void* InternalGetFragmentDataPtr(FMassEntityHandle EntityHandle, const UScriptStruct* FragmentType) const;
	/** Note that it's the caller's responsibility to ensure `ConstSharedFragmentType` is a kind of FMassSharedFragment */
	UE_API const FConstSharedStruct* InternalGetConstSharedFragmentPtr(FMassEntityHandle EntityHandle, const UScriptStruct* ConstSharedFragmentType) const;
	/** Note that it's the caller's responsibility to ensure `SharedFragmentType` is a kind of FMassSharedFragment */
	UE_API const FSharedStruct* InternalGetSharedFragmentPtr(FMassEntityHandle EntityHandle, const UScriptStruct* SharedFragmentType) const;

	// 中文：BatchCreate 系列的共同私有路径——验证所有 entity 都是 Reserved 状态后批量绑 archetype +
	//      archetype.BatchAddEntities + 注册到 CreationContext。
	TSharedRef<FEntityCreationContext> InternalBatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities);

	// 中文：把 EntityStorage（variant）抽象成 FStorageType& 接口的访问辅助。
	//      WITH_MASS_CONCURRENT_RESERVE=1 时通过 Visit 选择具体实现；否则直接 Get。
	//      未初始化时调用会 check fail。
	UE::Mass::FStorageType& GetEntityStorageInterface() const
	{
#if WITH_MASS_CONCURRENT_RESERVE
		using namespace UE::Mass;
		struct StorageSelector
		{
			UE::Mass::IEntityStorageInterface* operator()(FEmptyVariantState&) const
			{
				checkf(false, TEXT("Attempt to use EntityStorageInterface without initialization"));
				return nullptr;
			}
			UE::Mass::IEntityStorageInterface* operator()(FSingleThreadedEntityStorage& Storage) const
			{
				return &Storage;
			}
			UE::Mass::IEntityStorageInterface* operator()(FConcurrentEntityStorage& Storage) const
			{
				return &Storage;
			}
		};

		UE::Mass::IEntityStorageInterface* Interface = Visit(StorageSelector{}, EntityStorage);

		return *Interface;
#else	
		return EntityStorage.Get<UE::Mass::FSingleThreadedEntityStorage>();
#endif
	}

	bool DebugDoCollectionsOverlapCreationContext(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections) const;
		
private:

	friend struct UE::Mass::Private::FEntityStorageInitializer;
	// 中文：EntityStorage variant 持有 entity 索引数据。三种状态：
	//   - FEmptyVariantState：未 Initialize；
	//   - FSingleThreadedEntityStorage：默认，无锁，性能最好；
	//   - FConcurrentEntityStorage：可在工作线程并发 Reserve（仅 WITH_MASS_CONCURRENT_RESERVE=1 才编译进来）。
	using FEntityStorageContainerType = TVariant<
		FEmptyVariantState,
		UE::Mass::FSingleThreadedEntityStorage,
		UE::Mass::FConcurrentEntityStorage>;
	mutable FEntityStorageContainerType EntityStorage;
#if WITH_MASSENTITY_DEBUG
	// 中文：调试用快捷指针，避免每次 GetEntityStorageInterface 的 variant Visit 开销。
	UE::Mass::FStorageType* DebugEntityStoragePtr = nullptr;
#endif // WITH_MASSENTITY_DEBUG

	// 中文：activeprocessor 计数（atomic 因为 processor 调度器可能多线程）。>0 时同步 API 禁用。
	std::atomic<int32> ProcessingScopeCount = 0;

	// the "version" number increased every time an archetype gets added
	// 中文：archetype 集合的"逻辑版本号"，每新增一个 +1（在 CreateArchetype 中递增）。
	//      Query 缓存自身已扫到的 version，下次只检查更新部分——这是"O(1) 增量 query 匹配"的基石。
	uint32 ArchetypeDataVersion = 0;

	// Map of hash of sorted fragment list to archetypes with that hash
	// 中文：composition hash → 该 hash 下所有 archetype（hash 冲突需 IsEquivalent 二次比较）。
	//      用于 CreateArchetype 时的"已存在则复用"逻辑（archetype 自动去重的核心）。
	TMap<uint32, TArray<TSharedPtr<FMassArchetypeData>>> FragmentHashToArchetypeMap;

	// Map to list of archetypes that contain the specified fragment type
	// 中文：fragment 类型 → 包含它的所有 archetype。Query 系统可借此快速预筛"任意 archetype 包含 X"
	//      场景，避免遍历 AllArchetypes。
	TMap<const UScriptStruct*, TArray<TSharedPtr<FMassArchetypeData>>> FragmentTypeToArchetypeMap;

	// Contains all archetypes ever created. The array always growing and a given archetypes remains at a given index 
	// throughout its lifetime, and the index is never reused for another archetype. 
	// 中文：所有曾被创建过的 archetype（按创建顺序）。仅追加，永不收缩、永不移位——使得
	//      "ArchetypeIndex == ArchetypeDataVersion - 1"恒等，可靠地支持增量遍历。
	//      即便所有 entity 都被销毁，archetype 元数据也保留（带回 chunk 后再用便宜）。
	TArray<TSharedPtr<FMassArchetypeData>> AllArchetypes;

	/**
	 * This is a struct wrapping shared fragment management to ensure consistency between how
	 * shared and const shared fragment are added and fetched, across all the functions that do that
	 *
	 * 中文：【SharedFragment 池容器】
	 *  内部三张表协同：
	 *    - Instances：所有共享 fragment 实例，按 add 顺序排列；
	 *    - HashToInstanceIndexMap：CRC hash → Instances 中的 index（multimap 容忍 hash 冲突）；
	 *    - TypeToInstanceMap：type → 该类型所有实例（用于 ForEachSharedFragment）。
	 *
	 *  FindOrAdd 算法：
	 *    1. 查 HashToInstanceIndexMap 拿到候选 index（可能多个）；
	 *    2. 对每个候选用 CompareScriptStruct 做"按字段值"对比，命中则复用；
	 *    3. 否则 new instance 入池，注册到三张表。
	 *
	 *  注意 InstancesOfType.Add(Instances[Index]) 是拷贝 FSharedStruct（含 shared ptr），
	 *  数据本体不复制——FSharedStruct 内部用 shared pointer 指向真实内存。
	 */
	template<typename TSharedStructType>
	struct TSharedFragmentsContainer
	{
		TArray<TSharedStructType>* Find(const UScriptStruct* Type)
		{
			return TypeToInstanceMap.Find(Type);
		}

		TSharedStructType& FindOrAdd(const uint32 Hash, const UScriptStruct* Type, const uint8* Data)
		{
			for (TMultiMap<uint32, int32>::TConstKeyIterator It = HashToInstanceIndexMap.CreateConstKeyIterator(Hash); It; ++It)
			{
				TSharedStructType& Instance = Instances[It.Value()];

				if (Instance.GetScriptStruct() == Type
					&& Type->CompareScriptStruct(Instance.GetMemory(), Data, PPF_None))
				{
					return Instance;
				}
			}

			int32 Index = Add(TSharedStructType::Make(Type, Data));
			HashToInstanceIndexMap.Add(Hash, Index);
			return Instances[Index];
		}

		template<typename T, typename... TArgs>
		TSharedStructType& FindOrAdd(const uint32 Hash, TArgs&&... InArgs)
		{
			// Need to actually construct the struct to make proper comparison to possible existing instance
			TSharedStructType TempInstance = Make<T>(Forward<TArgs>(InArgs)...);

			for (TMultiMap<uint32, int32>::TConstKeyIterator It = HashToInstanceIndexMap.CreateConstKeyIterator(Hash); It; ++It)
			{
				TSharedStructType& Instance = Instances[It.Value()];

				if (Instance.GetScriptStruct() == T::StaticStruct()
					&& T::StaticStruct()->CompareScriptStruct(Instance.GetMemory(), TempInstance.GetMemory(), PPF_None))
				{
					return Instance;
				}
			}

			int32 Index = Add(MoveTemp(TempInstance));
			HashToInstanceIndexMap.Add(Hash, Index);
			return Instances[Index];
		}

		TSharedStructType& operator[](const int32 Index)
		{
			return Instances[Index];
		}

		TArrayView<TSharedStructType> GetAllInstances()
		{
			return Instances;
		}

	private:
		int32 Add(TSharedStructType&& SharedStruct)
		{
			TArray<TSharedStructType>& InstancesOfType = TypeToInstanceMap.FindOrAdd(SharedStruct.GetScriptStruct(), {});
			const int32 Index = Instances.Add(Forward<TSharedStructType>(SharedStruct));
			// note that even though we're copying the input F[Const]SharedStruct instance it's perfectly fine since 
			// F[Const]SharedStruct does guarantee there's not going to be data duplication (via a member shared pointer to hosted data)
			InstancesOfType.Add(Instances[Index]);
			return Index;
		}

		TArray<TSharedStructType> Instances;
		// Hash/Index in array pair
		TMultiMap<uint32, int32> HashToInstanceIndexMap;
		// Maps specific struct type to a collection of FSharedStruct instances of that type
		TMap<const UScriptStruct*, TArray<TSharedStructType>> TypeToInstanceMap;
	};

	// 中文：const shared fragment 池（entity 持只读引用）。
	TSharedFragmentsContainer<FConstSharedStruct> ConstSharedFragmentsContainer;
	// 中文：shared fragment 池（entity 持可写引用）。
	TSharedFragmentsContainer<FSharedStruct> SharedFragmentsContainer;

	// 中文：内嵌的观察者管理器。设计选择是"内嵌而非外部子系统"——因为几乎所有 entity
	//      变更都要触发观察者通知，强耦合不如直接持有；同时方便 lock/creation context
	//      与 EntityManager 的同步。
	FMassObserverManager ObserverManager;

	// 中文：类型信息中心。用 SharedRef 持有，因为子模块（RelationManager 等）也想拿 weak ref。
	TSharedRef<UE::Mass::FTypeManager> TypeManager;
	// 中文：关系管理。RelationManager 引用 EntityManager 通过构造时注入的 *this。
	UE::Mass::FRelationManager RelationManager;

	// 中文：archetype group 字典：FName → group type index（GroupTypes 数组下标）。
	TMap<const FName, const int32> GroupNameToTypeIndex;
	// @todo we'll probably have some "GroupTypeInformation" here in the future
	// 中文：group type index → group name。当前只是名字，未来可能附加类型元数据。
	TArray<const FName> GroupTypes;

#if WITH_MASSENTITY_DEBUG
	// 中文：调试期间检测"processor 是否合法访问其声明 require 的 fragment"——
	//      防止开发者在 processor 内偷偷读没声明的 fragment（导致并行调度出错）。
	FMassRequirementAccessDetector RequirementAccessDetector;
	FString DebugName;
	// 中文：当前启用的调试特性集合（如 trace processors 名字）。
	EDebugFeatures EnabledDebugFeatures = EDebugFeatures::All;
#endif // WITH_MASSENTITY_DEBUG

	// 中文：拥有者 UObject（通常是 FMassEntitySubsystem 或外部系统）。Weak 指针避免循环引用。
	//      可以为空——独立 EntityManager 实例（测试/工具）合法。
	TWeakObjectPtr<UObject> Owner;

	// 中文：archetype 创建广播事件。Query 系统订阅它做缓存增量更新。
	FOnNewArchetypeDelegate OnNewArchetypeEvent;

	// 中文：CoreDelegates::OnPostFork 的句柄，析构时移除以避免悬挂回调。
	FDelegateHandle OnPostForkHandle;

	/**
	 * This index will be enough to control which buffer is available for pushing commands since flashing is taking place 
	 * in the game thread and pushing commands to the buffer fetched by Defer() is only supported also on the game thread
	 * (due to checking the cached thread ID).
	 * The whole CL aims to support non-mass code trying to push commands while the flushing is going on (as triggered
	 * by MassObservers reacting to the commands being flushed currently).
	 *
	 * 中文：【双 CommandBuffer 的设计动因】
	 *  Mass 处理器调度链：FlushCommands → 命令逐个执行 → 某命令触发 observer →
	 *  observer 回调中又 Defer().Push 新命令。如果只有一个 buffer，新命令会写到正在被 flush
	 *  的容器，要么导致迭代器失效，要么需要额外锁。
	 *
	 *  解决：双 buffer。flush 开始时立刻 OpenedCommandBufferIndex++（mod 2），新命令进 buffer B，
	 *  正在 flush 的 buffer A 不再被写入。flush 完后检查 buffer B 是否非空（observer 添加的命令），
	 *  非空则继续 flush B（再次切换到 A），如此循环最多 5 次保证收敛。
	 *
	 *  push 端线程限制：Defer() 拿到的 buffer 内部检查 cached thread ID，仅 GameThread 可 push
	 *  （processor 也是 GameThread 调度，OK）。
	 */
	static constexpr int32 NumCommandBuffers = 2;
	// 中文：双 buffer 数组，固定长度。
	TStaticArray<TSharedPtr<FMassCommandBuffer>, NumCommandBuffers> DeferredCommandBuffers;
	// 中文：当前"正在收集命令"的 buffer 下标。Defer() 总是返回它。
	uint8 OpenedCommandBufferIndex = 0;
	// 中文：true 表示 flush 正在进行，再次调用 FlushCommands 直接 early-out（避免重入）。
	std::atomic<bool> bCommandBufferFlushingInProgress = false;
	// 中文：是否首次 flush（某些初始化逻辑只在第一次 flush 时执行）。
	bool bFirstCommandFlush = true;

	enum class EInitializationState : uint8
	{
		Uninitialized,  // 中文：构造完但未 Initialize
		Initialized,    // 中文：Initialize() 完成，可正常使用
		Deinitialized   // 中文：Deinitialize() 完成，不再可用
	};

	// 中文：当前初始化状态。AddReferencedObjects 等回调会根据它决定是否安全访问内部数据。
	EInitializationState InitializationState = EInitializationState::Uninitialized;

	//-----------------------------------------------------------------------------
	// DEPRECATED
	//-----------------------------------------------------------------------------
protected:
	UE_DEPRECATED(5.6, "This flavor of InternalCreateSimilarArchetype is deprecated due to the introduction of archetype grouping. Use InternalCreateSimilarArchetype with a FArchetypeGroups parameter instead")
	UE_API FMassArchetypeHandle InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition);
public:
	UE_DEPRECATED(5.6, "SetEntityFragmentsValues is deprecated. Use SetEntityFragmentValues instead (note the slight change in name).")
	UE_API void SetEntityFragmentsValues(FMassEntityHandle EntityHandle, TArrayView<const FInstancedStruct> FragmentInstanceList);

	UE_DEPRECATED(5.6, "Static BatchSetEntityFragmentsValues is deprecated. Use EntityManager's member function BatchSetEntityFragmentValues (note the slight change in name).")
	static UE_API void BatchSetEntityFragmentsValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList);

	UE_DEPRECATED(5.6, "Static BatchSetEntityFragmentsValues is deprecated. Use EntityManager's member function BatchSetEntityFragmentValues (note the slight change in name).")
	static UE_API void BatchSetEntityFragmentsValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList);

	template<UE::Mass::CConstSharedFragment T>
	UE_DEPRECATED(5.6, "Using ForEachSharedFragment for Const Shared Fragments has been deprecated. Use ForEachConstSharedFragment instead.")
	void ForEachSharedFragment(TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
	{
	}

	template<UE::Mass::CConstSharedFragment T>
	UE_DEPRECATED(5.6, "Using ForEachSharedFragmentConditional for Const Shared Fragments has been deprecated. Use ForEachConstSharedFragmentConditional instead.")
	void ForEachSharedFragmentConditional(TFunctionRef< bool(T& /*SharedFragment*/) > ConditionFunction, TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
	{
	}
};

//-----------------------------------------------------------------------------
// INLINE
//-----------------------------------------------------------------------------

#if WITH_MASSENTITY_DEBUG
ENUM_CLASS_FLAGS(FMassEntityManager::EDebugFeatures);
#endif

template<UE::Mass::CSharedFragment T>
void FMassEntityManager::ForEachSharedFragment(TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
{
	if (TArray<FSharedStruct>* InstancesOfType = SharedFragmentsContainer.Find(T::StaticStruct()))
	{
		for (const FSharedStruct& SharedStruct : *InstancesOfType)
		{
			ExecuteFunction(SharedStruct.Get<T>());
		}
	}
}

template<UE::Mass::CSharedFragment T>
void FMassEntityManager::ForEachSharedFragmentConditional(TFunctionRef< bool(T& /*SharedFragment*/) > ConditionFunction, TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
{
	if (TArray<FSharedStruct>* InstancesOfType = SharedFragmentsContainer.Find(T::StaticStruct()))
	{
		for (const FSharedStruct& SharedStruct : *InstancesOfType)
		{
			T& StructInstanceRef = SharedStruct.Get<T>();
			if (ConditionFunction(StructInstanceRef))
			{
				ExecuteFunction(StructInstanceRef);
			}
		}
	}
}

template<UE::Mass::CConstSharedFragment T>
void FMassEntityManager::ForEachConstSharedFragment(TFunctionRef< void(const T& /*ConstSharedFragment*/) > ExecuteFunction)
{
	if (TArray<FConstSharedStruct>* InstancesOfType = ConstSharedFragmentsContainer.Find(T::StaticStruct()))
	{
		for (const FConstSharedStruct& SharedStruct : *InstancesOfType)
		{
			ExecuteFunction(SharedStruct.Get<const T>());
		}
	}
}

template<UE::Mass::CConstSharedFragment T>
void FMassEntityManager::ForEachConstSharedFragmentConditional(TFunctionRef< bool(const T& /*ConstSharedFragment*/) > ConditionFunction, TFunctionRef< void(const T& /*ConstSharedFragment*/) > ExecuteFunction)
{
	if (TArray<FConstSharedStruct>* InstancesOfType = ConstSharedFragmentsContainer.Find(T::StaticStruct()))
	{
		for (const FConstSharedStruct& SharedStruct : *InstancesOfType)
		{
			const T& StructInstanceRef = SharedStruct.Get<const T>();
			if (ConditionFunction(StructInstanceRef))
			{
				ExecuteFunction(StructInstanceRef);
			}
		}
	}
}

inline const UE::Mass::FTypeManager& FMassEntityManager::GetTypeManager() const
{
	return *TypeManager;
}

inline UE::Mass::FTypeManager& FMassEntityManager::GetTypeManager()
{
	return *TypeManager;
}

inline bool FMassEntityManager::IsDuringEntityCreation() const
{
	return ObserverManager.GetCreationContext().IsValid();
}

inline TSharedRef<FMassObserverManager::FObserverLock> FMassEntityManager::GetOrMakeObserversLock()
{
	return ObserverManager.GetOrMakeObserverLock();
}

inline UE::Mass::FRelationManager& FMassEntityManager::GetRelationManager()
{
	return RelationManager;
}

template<UE::Mass::CRelation T>
bool FMassEntityManager::BatchCreateRelations(TArrayView<FMassEntityHandle> Subjects, TArrayView<FMassEntityHandle> Objects)
{
	return BatchCreateRelations(UE::Mass::FTypeManager::MakeTypeHandle(T::StaticStruct()), Subjects, Objects);
}

#undef UE_API
