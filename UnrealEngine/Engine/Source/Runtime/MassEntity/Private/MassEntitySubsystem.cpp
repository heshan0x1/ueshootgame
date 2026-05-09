// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a 实现侧）：
//   UMassEntitySubsystem 的具体实现。除了把 Initialize/PostInitialize/Deinitialize
//   委托给 FMassEntityManager 之外，还做两件事：
//     1) 通过 CVar 选择 EntityManager 的 Storage 模式：
//          * Mass.ConcurrentReserve.Enable / MaxEntityCount / EntitiesPerPage
//        当 WITH_MASS_CONCURRENT_RESERVE 编译开关开启 + 运行期 CVar=true 时，使用
//        并发安全的 reserve 实现；否则退回 SingleThreaded 实现。
//     2) 注册一个调试控制台命令 EntityManager.PrintArchetypes，用于打印当前 World
//        所有 Archetype 的诊断信息。
// =============================================================================

#include "MassEntitySubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassEntitySubsystem)

namespace UE::Mass::Private
{
	/**
	 * 中文：是否启用"并发 reserve"运行期能力（仅 WITH_MASS_CONCURRENT_RESERVE 编译开启时有效）。
	 *       开启后，EntityManager 内部的实体存储使用并发安全的分页结构，允许多线程同时
	 *       预留实体 ID（提升大批量生成场景的吞吐）。
	 */
	static bool bEnableMassConcurrentReserveRuntime = true;

	/** 中文：并发 reserve 模式下，全局允许的最大实体数。必须是 2 的幂。默认 2^27 ≈ 1.3 亿。 */
	static int32 ConcurrentReserveMaxEntityCount = 1 << 27;

	/** 中文：并发 reserve 模式下，每个 page 的实体上限。必须是 2 的幂。默认 2^16 = 65536。
	 *       page 越大：固定分页元数据开销越小，但单次 page 分配需要越大的连续内存块。 */
	static int32 ConcurrentReserveMaxEntitiesPerPage = 1 << 16;

	namespace
	{
		/**
		 * 中文：把上述 3 个变量暴露给控制台 / .ini 配置。
		 *   匿名命名空间使其符号在本翻译单元内本地可见，避免全局符号冲突。
		 */
		FAutoConsoleVariableRef CVars[] = {
			{
				TEXT("Mass.ConcurrentReserve.Enable"),
				bEnableMassConcurrentReserveRuntime,
				TEXT("Enable Mass's concurrent reserve feature in runtime"),
				ECVF_Default
			},
			{
				TEXT("Mass.ConcurrentReserve.MaxEntityCount"),
				ConcurrentReserveMaxEntityCount,
				TEXT("Set maximum number of permissible entities.  Must be power of 2."),
				ECVF_Default
			},
			{
				TEXT("Mass.ConcurrentReserve.EntitiesPerPage"),
				ConcurrentReserveMaxEntitiesPerPage,
				TEXT("Set number of entities per page. Must be power of 2. Larger reduces fixed memory overhead of FEntityData page lookup but requires bigger contiguous memory blocks per page"),
				ECVF_Default
			}
		};
	}
}

//-----------------------------------------------------------------------------
// UMassEntitySubsystem
//-----------------------------------------------------------------------------

/**
 * 中文：构造函数——直接 new 一个 FMassEntityManager 并 wrap 进 TSharedPtr。
 *   * 此时 EntityManager 还没 Initialize（构造只是分配对象），真正的初始化在 ::Initialize() 中完成。
 *   * 把 this（UMassEntitySubsystem*）作为 Owner 传入，让 EntityManager 能反向访问宿主子系统
 *     （某些场景需要拿到 World/Outer）。
 */
UMassEntitySubsystem::UMassEntitySubsystem()
	: EntityManager(MakeShareable(new FMassEntityManager(this)))
{
	
}

/**
 * 中文：内存统计——在 Super 的基础上，把 EntityManager 自身的资源大小累加进 CumulativeResourceSize。
 *   被引擎的内存分析工具（如 ListUObjectMemUsage、obj list）调用。
 */
void UMassEntitySubsystem::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	EntityManager->GetResourceSizeEx(CumulativeResourceSize);
}

/**
 * 中文：Initialize 流程——
 *   1) 走 Super::Initialize（UMassSubsystemBase 的标准簿记 + ensure；它内部会判断
 *      "我自己就是 UMassEntitySubsystem"从而跳过自动注册——避免 InitializeDependency 自循环）。
 *   2) 根据编译开关 + CVar 选择 Storage 模式：
 *        WITH_MASS_CONCURRENT_RESERVE && bEnableMassConcurrentReserveRuntime
 *           → FMassEntityManager_InitParams_Concurrent（并发分页存储）
 *        否则
 *           → FMassEntityManager_InitParams_SingleThreaded（单线程存储）
 *   3) 调 EntityManager->Initialize(InitParams)，让 EntityManager 完成 storage 创建、
 *      默认 Processor 注册等。
 *   4) 调 HandleLateCreation()——若该子系统是在 World BeginPlay 之后才被创建出来
 *      （GameplayFeatureActions 等动态加载场景），立即补打 PostInitialize / OnWorldBeginPlay。
 *   5) 显式注册自己（UMassEntitySubsystem 类）到 FTypeManager。
 *      因为 Super 跳过了 UMassEntitySubsystem 自己的注册，必须手动补。
 *
 * 编译开关：
 *   #if WITH_MASS_CONCURRENT_RESERVE  控制是否编入并发 reserve 代码路径。
 */
void UMassEntitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FMassEntityManagerStorageInitParams InitializationParams;
#if WITH_MASS_CONCURRENT_RESERVE
	if (UE::Mass::Private::bEnableMassConcurrentReserveRuntime)
	{
		// 中文：使用并发安全的分页存储，参数从 CVar 读取。
		InitializationParams.Emplace<FMassEntityManager_InitParams_Concurrent>(
			FMassEntityManager_InitParams_Concurrent
			{
				.MaxEntityCount = static_cast<uint32>(UE::Mass::Private::ConcurrentReserveMaxEntityCount),
				.MaxEntitiesPerPage = static_cast<uint32>(UE::Mass::Private::ConcurrentReserveMaxEntitiesPerPage)
			});
	}
	else
#endif // WITH_MASS_CONCURRENT_RESERVE
	{
		// 中文：单线程存储——简单 contiguous array，无并发开销。CVar 关闭或编译未启用时使用。
		InitializationParams.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
	}
	
	EntityManager->Initialize(InitializationParams);
	HandleLateCreation();

	// 中文：自我登记到 FTypeManager。使用 FSubsystemTypeTraits::Make<UMassEntitySubsystem>() 生成默认 traits。
	UE::Mass::Subsystems::RegisterSubsystemType(EntityManager.ToSharedRef(), GetClass(), UE::Mass::FSubsystemTypeTraits::Make<UMassEntitySubsystem>());
}

/**
 * 中文：PostInitialize——委托给 EntityManager。
 *   * 之所以放在 PostInitialize 而非 Initialize 内，是因为有些 Processor 在初始化时会
 *     反过来访问其他子系统；而 PostInitialize 由 USubsystemCollection 保证"所有子系统的
 *     Initialize 都已结束"——这样 Processor 初始化中拿到的依赖是稳定的。
 */
void UMassEntitySubsystem::PostInitialize()
{
	Super::PostInitialize();
	// this needs to be done after all the subsystems have been initialized since some processors might want to access
	// them during processors' initialization
	EntityManager->PostInitialize();
}

/**
 * 中文：Deinitialize 顺序——
 *   1) EntityManager->Deinitialize() 先释放业务资源（Processor 解注册、销毁所有 Archetype 等）；
 *   2) EntityManager.Reset() 释放共享指针引用计数；
 *   3) Super::Deinitialize() 最后做 UMassSubsystemBase 的清理（重置 InitializationState）。
 *   该顺序保证 Processor / Observer / Subsystem 注销逻辑执行时，EntityManager 仍然存活。
 */
void UMassEntitySubsystem::Deinitialize()
{
	EntityManager->Deinitialize();
	EntityManager.Reset();
	Super::Deinitialize();
}

#if WITH_MASSENTITY_DEBUG
//-----------------------------------------------------------------------------
// Debug commands
// 中文：仅在 WITH_MASSENTITY_DEBUG 编译开启时才编入的调试命令。
//-----------------------------------------------------------------------------

/**
 * 中文：注册控制台命令 EntityManager.PrintArchetypes。
 *   用法（在控制台输入）：EntityManager.PrintArchetypes
 *   作用：在当前 World 上找到 UMassEntitySubsystem，调用 EntityManager 的 DebugPrintArchetypes
 *        把所有 Archetype（实体类型集合）的诊断信息打到 Output。
 *   失败：如果当前 World 上没有 UMassEntitySubsystem，输出错误日志。
 *
 * 实现细节：FAutoConsoleCommandWithWorldArgsAndOutputDevice 对象作为静态全局，会在模块加载时
 *          自动注册控制台命令；模块卸载时自动注销。
 */
FAutoConsoleCommandWithWorldArgsAndOutputDevice GPrintArchetypesCmd(
	TEXT("EntityManager.PrintArchetypes"),
	TEXT("Prints information about all archetypes in the current world"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
		{
			if (const UMassEntitySubsystem* EntitySubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr)
			{
				EntitySubsystem->GetEntityManager().DebugPrintArchetypes(Ar);
			}
			else
			{
				Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find Entity Subsystem for world %s"), *GetPathNameSafe(World));
			}
		}));
#endif // WITH_MASSENTITY_DEBUG