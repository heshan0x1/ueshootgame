// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavSystemConfigOverride.cpp
// -----------------------------------------------------------------------------
// ANavSystemConfigOverride 的实现。
// 三条主干：
//   ApplyConfig()  ── 读 OverridePolicy，分派到：
//       Override → OverrideNavSystem()：销毁旧 NavSys + 新建
//       Append   → AppendToNavSystem()：往旧 NavSys 追加 Config
//       Skip     → 什么也不做
//
//   OverrideNavSystem() 逻辑要点：
//     1) 把 Config 写进 WorldSettings（让后续 World 初始化流程能拿到）
//     2) 调 FNavigationSystem::AddNavigationSystemToWorld(... bOverridePreviousNavSys=true)
//     3) 编辑器模式下需要延后一帧 InitializeForWorld，避免在 UObject 注册中途改 World 状态
//
// 与架构文档 4.1 节的 "PostInitProperties → ConditionalPopulateNavOctree" 联动：
// 若本 Actor 在 World 初始化之前被发现，会把 Config 塞进 WorldSettings；之后 UWorld
// 构造 NavigationSystem 时会直接用这个 Config 的 NavigationSystemClass。
// =============================================================================

#include "NavSystemConfigOverride.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavSystemConfigOverride)

#if WITH_EDITORONLY_DATA
#include "UObject/ConstructorHelpers.h"
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#endif // WITH_EDITORONLY_DATA


// 构造：Static SceneRoot + 编辑器下一个 Billboard 图标；默认不走客户端。
ANavSystemConfigOverride::ANavSystemConfigOverride(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	// World Partition 不空间化加载本 Actor —— 它应始终存在于关卡
	bIsSpatiallyLoaded = false;
	
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteTextureObject;
			FName ID_Notes;
			FText NAME_Notes;
			FConstructorStatics()
				: NoteTextureObject(TEXT("/Engine/EditorResources/S_Note"))
				, ID_Notes(TEXT("Notes"))
				, NAME_Notes(NSLOCTEXT("SpriteCategory", "Notes", "Notes"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (SpriteComponent)
		{
			SpriteComponent->Sprite = ConstructorStatics.NoteTextureObject.Get();
			SpriteComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Notes;
			SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Notes;
			SpriteComponent->SetupAttachment(RootComponent);
			SpriteComponent->Mobility = EComponentMobility::Static;
		}
	}
#endif // WITH_EDITORONLY_DATA

	SetHidden(true);
	SetCanBeDamaged(false);

	// 默认不下发客户端；通过 bLoadOnClient 让用户按需启用
	bNetLoadOnClient = false;
}

// 游戏世界入口：BeginPlay 时应用配置
void ANavSystemConfigOverride::BeginPlay()
{
	Super::BeginPlay();
	ApplyConfig();
}

// 调度函数：根据 OverridePolicy 决定走 Override / Append / Skip。
// 若当前 World 尚无 NavSys，一律走 OverrideNavSystem（构建新实例）。
void ANavSystemConfigOverride::ApplyConfig()
{
	UWorld* World = GetWorld();
	if (World)
	{
		UNavigationSystemBase* PrevNavSys = World->GetNavigationSystem();

		// 没有旧 NavSys 或策略为 Override 时：创建/替换
		if (PrevNavSys == nullptr || OverridePolicy == ENavSystemOverridePolicy::Override)
		{
			OverrideNavSystem();
		}
		// PrevNavSys != null at this point
		else if (OverridePolicy == ENavSystemOverridePolicy::Append)
		{
			// take the prev nav system and append data to it
			AppendToNavSystem(*PrevNavSys);
		}
		// else PrevNavSys != null AND OverridePolicy == ENavSystemOverridePolicy::Skip, so ignoring the override
	}
}

void ANavSystemConfigOverride::PostInitProperties()
{
	Super::PostInitProperties();
}

// Append 策略：让旧 NavSys 把本 Override Actor 的 Config 里的信息 (典型：SupportedAgents) 合入自身。
void ANavSystemConfigOverride::AppendToNavSystem(UNavigationSystemBase& PrevNavSys)
{
	if (NavigationSystemConfig)
	{
		PrevNavSys.AppendConfig(*NavigationSystemConfig);
	}
}

// Override 策略核心实现：
//   1) 把 Config 写到 WorldSettings（持久性的 override 来源）
//   2) 调 AddNavigationSystemToWorld(bOverridePreviousNavSys=true)，会销毁旧 NavSys + 新建
//   3) 编辑器模式下 Initialize 延迟到下一帧再跑，避免组件注册过程中同步重入
void ANavSystemConfigOverride::OverrideNavSystem()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	AWorldSettings* WorldSetting = World->GetWorldSettings();
	if (WorldSetting)
	{
		// 写入 WorldSettings：即便我们现在不触发，后续 World 重建 NavSys 时也能用上
		WorldSetting->SetNavigationSystemConfigOverride(NavigationSystemConfig);
	}

	if (World->bIsWorldInitialized
		&& NavigationSystemConfig)
	{
		// 根据 WorldType 决定 RunMode：Editor / PIE / Game
		const FNavigationSystemRunMode RunMode = World->WorldType == EWorldType::Editor
			? FNavigationSystemRunMode::EditorMode
			: (World->WorldType == EWorldType::PIE
				? FNavigationSystemRunMode::PIEMode
				: FNavigationSystemRunMode::GameMode)
			;

		if (FNavigationSystem::IsEditorRunMode(RunMode))
		{
			// 编辑器：先注入不 Initialize，下一帧再 Initialize（避免在 Tick 内部销毁-重建）
			FNavigationSystem::AddNavigationSystemToWorld(*World, RunMode, NavigationSystemConfig, /*bInitializeForWorld=*/false, /*bOverridePreviousNavSys=*/true);
#if WITH_EDITOR
			UNavigationSystemBase* NewNavSys = World->GetNavigationSystem();
			if (NewNavSys)
			{
				GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(this
					, &ANavSystemConfigOverride::InitializeForWorld, NewNavSys, World, RunMode));
			}
#endif // WITH_EDITOR
		}
		else
		{
			// Game/PIE：同步 Initialize
			FNavigationSystem::AddNavigationSystemToWorld(*World, RunMode, NavigationSystemConfig, /*bInitializeForWorld=*/true, /*bOverridePreviousNavSys=*/true);
		}
	}
}

#if WITH_EDITOR
// 编辑器 World 入口：组件注册完成后应用 Config。
// 游戏 World 走 BeginPlay，这里显式过滤掉。
void ANavSystemConfigOverride::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// ApplyConfig in PostRegisterAllComponents only for Editor worlds; applied in BeginPlay for Game worlds.
	UWorld* World = GetWorld();
	if (World == nullptr || World->IsGameWorld() || World->WorldType == EWorldType::Inactive)
	{
		return;
	}

	// While refreshing streaming levels, components are unregistered then registered again
	// and we don't want to modify the configuration and recreate the NavigationSystem.
	// 流式刷新时的临时反注册-注册不应触发重建
	if (World->IsRefreshingStreamingLevels())
	{
		return;
	}

	// Config override should not be applied during cooking.
	// Cook 过程不触发——Cook 期间修改 World 状态会弄乱序列化
	if (GIsCookerLoadingPackage)
	{
		return;
	}

	ApplyConfig();
}

// 反注册路径：若本 Actor 是当前 Config 的来源，清空 override 并重建 NavSys。
void ANavSystemConfigOverride::PostUnregisterAllComponents()
{
	Super::PostUnregisterAllComponents();

	if (NavigationSystemConfig == nullptr)
	{
		return;
	}

	// ApplyConfig was performed in PostRegisterAllComponents for Editor worlds so nothing to unregister for Game worlds.
	UWorld* World = GetWorld();
	if (World == nullptr || World->IsGameWorld() || World->WorldType == EWorldType::Inactive || !World->IsInitialized() || World->IsBeingCleanedUp())
	{
		return;
	}

	// While refreshing streaming levels, components are unregistered then registered again
	// and we don't want to modify the configuration and recreate the NavigationSystem.
	if (World->IsRefreshingStreamingLevels())
	{
		return;
	}

	// Config override should not be applied during cooking.
	if (GIsCookerLoadingPackage)
	{
		return;
	}

	// If our override was used to create the navigation system, we remove the dependency
	// and recreate the navigation system (if needed after removing the override)
	// 若 WorldSettings 当前 override 正是我们这份 Config，则清掉并重建 NavSys（可能退回默认）。
	AWorldSettings* WorldSetting = World->GetWorldSettings();
	if (WorldSetting && WorldSetting->GetNavigationSystemConfigOverride() == NavigationSystemConfig)
	{
		WorldSetting->SetNavigationSystemConfigOverride(nullptr);
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode, nullptr, /*bInitializeForWorld=*/true, /*bOverridePreviousNavSys=*/true);
	}
}

// 延迟执行的 InitializeForWorld —— 在下一帧跑，避免组件注册中同步重建引发的 UObject 状态乱
void ANavSystemConfigOverride::InitializeForWorld(UNavigationSystemBase* NewNavSys, UWorld* World, const FNavigationSystemRunMode RunMode)
{
	if (NewNavSys && World)
	{
		NewNavSys->InitializeForWorld(*World, RunMode);
	}
}

// 编辑器手动按钮：强制重建 NavSys 以应用属性面板里的改动。
void ANavSystemConfigOverride::ApplyChanges()
{
	UWorld* World = GetWorld();
	if (World)
	{
		AWorldSettings* WorldSetting = World->GetWorldSettings();
		if (WorldSetting)
		{
			WorldSetting->SetNavigationSystemConfigOverride(NavigationSystemConfig);
		}

		// recreate nav sys
		World->SetNavigationSystem(nullptr);
		FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode, NavigationSystemConfig, /*bInitializeForWorld=*/true);
	}
}

// 同步 bLoadOnClient → bNetLoadOnClient（后者是 Actor 基类字段，前者是我们暴露给用户的）
void ANavSystemConfigOverride::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bNetLoadOnClient = bLoadOnClient;
}
#endif // WITH_EDITOR


