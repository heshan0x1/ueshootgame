// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件说明（Wave 9a 实现侧）：
//   提供 UMassSubsystemBase / UMassTickableSubsystemBase 的具体实现，并暴露：
//     * mass.RuntimeSubsystemsEnabled  CVar：一键开关所有 Mass 运行期子系统的自动创建。
//     * UE::Mass::Subsystems::RegisterSubsystemType（两个重载）：把一个 USubsystem 类
//       与其 FSubsystemTypeTraits 一起登记到 FTypeManager。
//     * UE::Mass::Private::HandleLateCreation：实际的"延迟创建补救"算法。
// =============================================================================

#include "MassSubsystemBase.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"

// UE 反射/网络代码生成器自动生成的 inlined 文件，必须放在所有 include 之后
#include UE_INLINE_GENERATED_CPP_BY_NAME(MassSubsystemBase)

namespace UE::Mass
{
	namespace Subsystems
	{
		/**
		 * 把 SubsystemClass + Traits 写入到给定 EntityManager 的 TypeManager 中。
		 * 中文说明：
		 *   * 这是"最终落地"重载——直接调用 FTypeManager::RegisterType。
		 *   * 调用方需自行确保 EntityManager 已经初始化（Wave M11 中 TypeManager 是
		 *     EntityManager 的内嵌成员）。
		 */
		void RegisterSubsystemType(TSharedRef<FMassEntityManager> EntityManager, TSubclassOf<USubsystem> SubsystemClass, FSubsystemTypeTraits&& Traits)
		{
			EntityManager->GetTypeManager().RegisterType(SubsystemClass, MoveTemp(Traits));
		}

		/**
		 * 通过 FSubsystemCollectionBase 拿到默认 EntityManager 后，再走上面的重载。
		 * 中文说明：
		 *   * InitializeDependency<UMassEntitySubsystem>() 会保证 UMassEntitySubsystem 比
		 *     当前调用者更早完成 Initialize（如果还没初始化，会立即触发它的 Initialize）。
		 *     这是 UE 子系统系统提供的标准 dependency 排序机制。
		 *   * 因此其他 Mass 子系统在 Initialize 中调用 RegisterSubsystemType(Collection, ...)
		 *     时，可以放心拿到一个已初始化的 EntityManager。
		 *   * 若该 World 上禁用了 Mass（CVar=false），InitializeDependency 可能返回 nullptr，
		 *     此时静默跳过——不会引入崩溃。
		 */
		void RegisterSubsystemType(FSubsystemCollectionBase& Collection, TSubclassOf<USubsystem> SubsystemClass, FSubsystemTypeTraits&& Traits)
		{
			if (UMassEntitySubsystem* EntitySubsystem = Collection.InitializeDependency<UMassEntitySubsystem>())
			{
				RegisterSubsystemType(EntitySubsystem->GetMutableEntityManager().AsShared(), SubsystemClass, MoveTemp(Traits));
			}
		}
	} // namespace UE::Mass::Subsystem

	namespace Private
	{
		/** 
		 * A helper function calling PostInitialize and OnWorldBeginPlay for the given subsystem, provided the world has already begun play.
		 * @see UMassSubsystemBase::HandleLateCreation for more detail
		 *
		 * 中文说明（延迟创建补救核心算法）：
		 *   * 输入：MassWorldSubsystem——当前要补救的子系统；InitializationState——它当前的阶段位图。
		 *   * 步骤：
		 *       1) 拿到 World。
		 *       2) 若 World 已 IsInitialized() 且 PostInitialize 还没调过，立即手动调 PostInitialize。
		 *          注意调到的是 *MassWorldSubsystem* 的虚函数，会让基类 InitializationState
		 *          的 bPostInitializeCalled 翻成 true（并通过 ensure 校验未重复调用）。
		 *       3) 若 World 已 HasBegunPlay() 且 OnWorldBeginPlay 还没调过，立即手动调
		 *          OnWorldBeginPlay。
		 *   * 之所以分两步而非一气呵成，是因为 UE 中 World 可能处于"已 Initialize 但还没
		 *     BeginPlay"的中间阶段（比如某些 PIE 流程）。
		 */
		void HandleLateCreation(UWorldSubsystem& MassWorldSubsystem, const UE::Mass::Subsystems::FInitializationState InitializationState)
		{
			// handle late creation
			UWorld* World = MassWorldSubsystem.GetWorld();
			if (World)
			{
				if (World->IsInitialized() == true && InitializationState.bPostInitializeCalled == false)
				{
					MassWorldSubsystem.PostInitialize();
				}
				if (World->HasBegunPlay() == true && InitializationState.bOnWorldBeginPlayCalled == false)
				{
					MassWorldSubsystem.OnWorldBeginPlay(*World);
				}
			}
		}

		/**
		 * 中文：mass.RuntimeSubsystemsEnabled 的存储变量。
		 *   true（默认）：允许 Mass 子系统在游戏运行期自动创建；
		 *   false       ：禁用所有 UMassSubsystemBase / UMassTickableSubsystemBase 子类的自动创建。
		 *   该变量必须在 World 加载之前设置才能完整生效（因为 ShouldCreateSubsystem 只在
		 *   子系统注册期间被询问一次）。
		 */
		bool bRuntimeSubsystemsEnabled = true;

		namespace
		{
			/**
			 * 中文：CVar 注册表。把 bRuntimeSubsystemsEnabled 暴露给控制台 / 配置系统，命名 mass.RuntimeSubsystemsEnabled。
			 *   匿名命名空间确保符号链接局部化，仅本翻译单元可见。
			 */
			FAutoConsoleVariableRef AnonymousCVars[] =
			{
				{ TEXT("mass.RuntimeSubsystemsEnabled")
				, bRuntimeSubsystemsEnabled
				, TEXT("true by default, setting to false will prevent auto-creation of game-time Mass-related subsystems. Needs to be set before world loading.")
				, ECVF_Default }
			};
		}
	} // UE::Mass::Private
} // namespace UE::Mass

//-----------------------------------------------------------------------------
// UMassSubsystemBase
//-----------------------------------------------------------------------------

/**
 * 中文：暴露给外部的"全局 Mass 运行期开关查询"。当前实现仅看 CVar；保留 Outer 形参以便
 *       将来按 World 类型（Editor/PIE/Game/Inactive）做更精细的过滤。
 */
bool UMassSubsystemBase::AreRuntimeMassSubsystemsAllowed(UObject* Outer)
{
	return UE::Mass::Private::bRuntimeSubsystemsEnabled;
}

/**
 * 中文：UE 子系统创建前的过滤入口。
 *       双重过滤：先看 Mass 自己的全局开关，再走 UWorldSubsystem 的默认逻辑（World 类型筛选）。
 */
bool UMassSubsystemBase::ShouldCreateSubsystem(UObject* Outer) const 
{
	return UMassSubsystemBase::AreRuntimeMassSubsystemsAllowed(Outer) && Super::ShouldCreateSubsystem(Outer);
}

/**
 * 中文：Initialize 实现。流程：
 *   1) Super::Initialize（UWorldSubsystem 标准流程）
 *   2) ensure 防御 bInitializeCalled 没被重复置位 → 翻成 true
 *   3) 若自己不是 UMassEntitySubsystem，则把"自身实际类"+默认 traits 登记到 FTypeManager。
 *      子类如有特殊 traits 应在自己的 Initialize() 里调用 OverrideSubsystemTraits<T>() 覆盖。
 *
 * 关键点（特例处理）：
 *   * UMassEntitySubsystem 自己就是 EntityManager 的宿主，如果它走到这里再去
 *     InitializeDependency<UMassEntitySubsystem>()，就是"自己依赖自己"。所以这里用
 *     IsChildOf 排除掉它，由 UMassEntitySubsystem::Initialize 中显式调用第二个重载完成注册。
 */
void UMassSubsystemBase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bInitializeCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bInitializeCalled = true;

	// register the given child class with default traits. Child-class can always override the traits data registered here.
	// Note that we're not performing the registration for UMassEntitySubsystem since that's the subsystem
	// we use to get access to the EntityManager instance in the first place. UMassEntitySubsystem has to perform the registration manually
	// 中文：跳过 UMassEntitySubsystem 自身的注册（避免 InitializeDependency 自循环）。
	if (GetClass()->IsChildOf(UMassEntitySubsystem::StaticClass()) == false)
	{
		// register the given child class with default traits. Child-class can always override the traits data registered here.
		UE::Mass::Subsystems::RegisterSubsystemType(Collection, GetClass(), UE::Mass::FSubsystemTypeTraits::Make<UMassSubsystemBase>());
	}
}

/** 中文：PostInitialize——仅做簿记 + ensure 防重复。 */
void UMassSubsystemBase::PostInitialize()
{
	Super::PostInitialize();

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bPostInitializeCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bPostInitializeCalled = true;
}

/**
 * 中文：Deinitialize——把 InitializationState 整体重置为默认（全 false）。
 *   重置发生在 Super::Deinitialize 之前，但实际上不影响任何顺序敏感的逻辑。
 *   重置可以让"同一个对象被复用"时再次走完整初始化（理论上 UE 不会复用，但保守起见）。
 */
void UMassSubsystemBase::Deinitialize()
{
	InitializationState = UE::Mass::Subsystems::FInitializationState();

	Super::Deinitialize();
}

/** 中文：World BeginPlay——簿记 + ensure 防重复。子类可 override 实现具体逻辑。 */
void UMassSubsystemBase::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bOnWorldBeginPlayCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bOnWorldBeginPlayCalled = true;
}

/** 中文：转发到 UE::Mass::Private::HandleLateCreation，把自身 + 当前 InitializationState 传过去。 */
void UMassSubsystemBase::HandleLateCreation()
{
	UE::Mass::Private::HandleLateCreation(*this, InitializationState);
}

//-----------------------------------------------------------------------------
// UMassTickableSubsystemBase
//-----------------------------------------------------------------------------
// 中文：以下实现与 UMassSubsystemBase 几乎逐行对应，差异：
//   * Initialize 中没有"排除 UMassEntitySubsystem"的特例分支，因为 Tickable 基类的子类
//     不会出现"自身就是 EntityManager 宿主"的情况。
//   * 注册时使用 FSubsystemTypeTraits::Make<UMassTickableSubsystemBase>()，让默认 traits
//     反映 Tickable 子系统的访问特征（与非 Tickable 可能有差异，由 Make<T> 模板决定）。
//-----------------------------------------------------------------------------

/** 中文：与 UMassSubsystemBase::ShouldCreateSubsystem 完全相同的过滤策略。 */
bool UMassTickableSubsystemBase::ShouldCreateSubsystem(UObject* Outer) const
{
	return UMassSubsystemBase::AreRuntimeMassSubsystemsAllowed(Outer) && Super::ShouldCreateSubsystem(Outer);
}

/**
 * 中文：Tickable 版的 Initialize。
 *   * 同样做簿记和 ensure。
 *   * 直接登记 GetClass()——这里没有"排除自己"的特例（因为 Tickable 基类不会承载 EntityManager）。
 */
void UMassTickableSubsystemBase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bInitializeCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bInitializeCalled = true;

	// register the given child class with default traits. Child-class can always override the traits data registered here.
	UE::Mass::Subsystems::RegisterSubsystemType(Collection, GetClass(), UE::Mass::FSubsystemTypeTraits::Make<UMassTickableSubsystemBase>());
}

/** 中文：簿记 + ensure。 */
void UMassTickableSubsystemBase::PostInitialize()
{
	Super::PostInitialize();

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bPostInitializeCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bPostInitializeCalled = true;
}

/** 中文：清空 InitializationState。 */
void UMassTickableSubsystemBase::Deinitialize()
{
	InitializationState = UE::Mass::Subsystems::FInitializationState();

	Super::Deinitialize();
}

/** 中文：簿记 + ensure。 */
void UMassTickableSubsystemBase::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// This ensure is here to make sure we handle HandleLateCreation() gracefully, we don't expect it to ever trigger unless users start to manually call the functions
	ensureMsgf(InitializationState.bOnWorldBeginPlayCalled == false, TEXT("%hs called multiple times"), __FUNCTION__);
	InitializationState.bOnWorldBeginPlayCalled = true;
}

/** 中文：与 UMassSubsystemBase::HandleLateCreation 等价。 */
void UMassTickableSubsystemBase::HandleLateCreation()
{
	UE::Mass::Private::HandleLateCreation(*this, InitializationState);
}
