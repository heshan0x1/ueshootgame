// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationSystemModule.cpp
// -----------------------------------------------------------------------------
// NavigationSystem 模块实现。
// 生命周期非常轻量：Startup/Shutdown 只在 WITH_EDITOR 下绑定两个引擎全局委托：
//   1) FWorldDelegates::OnPreWorldInitialization →
//      INavLinkCustomInterface::OnPreWorldInitialization
//      （在 World 初始化前做一次 Custom Link ID 回收 / 预处理）
//   2) FCoreDelegates::OnPostEngineInit →
//      ANavMeshBoundsVolume::OnPostEngineInit
//      （引擎初始化完成后，刷新 NavMeshBoundsVolume 的静态数据）
// 见架构文档 4.1 节。
// 注意：实际 NavigationSystem 实例的创建由 UWorld 根据 Project Settings 决定，
// 本模块当前 *并不* 直接创建 UNavigationSystemV1（下面注释掉的 CreateNavigationSystemInstance
// 是个遗留草稿）。
// =============================================================================

#include "NavigationSystemModule.h"
#include "EngineDefines.h"
#include "AI/NavigationSystemBase.h"
#include "Misc/CoreDelegates.h"
#include "Templates/SubclassOf.h"
#include "Engine/World.h"
#include "NavLinkCustomInterface.h"
#include "NavMesh/NavMeshBoundsVolume.h"

#define LOCTEXT_NAMESPACE "NavigationSystem"

DEFINE_LOG_CATEGORY_STATIC(LogNavSysModule, Log, All);

// 模块实现类：只提供 Startup/Shutdown 钩子。
class FNavigationSystemModule : public INavSysModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	//virtual UNavigationSystemBase* CreateNavigationSystemInstance(UWorld& World) override;
	// End IModuleInterface

private:
#if WITH_EDITOR 
	// 保存委托句柄以便 Shutdown 时精准反注册
	static FDelegateHandle OnPreWorldInitializationHandle;
	static FDelegateHandle OnPostEngineInitHandle;
#endif
};

IMPLEMENT_MODULE(FNavigationSystemModule, NavigationSystem)

#if WITH_EDITOR 
FDelegateHandle FNavigationSystemModule::OnPreWorldInitializationHandle;
FDelegateHandle FNavigationSystemModule::OnPostEngineInitHandle;
#endif

// StartupModule：编辑器模式下挂全局回调；运行时模式下无需处理（由 World 自行创建 NavSystem）。
void FNavigationSystemModule::StartupModule()
{ 
	// mz@todo bind to all the delegates in FNavigationSystem
#if WITH_EDITOR 
	// World 将要初始化之前 —— 让 Custom Link 系统做一些准备（如 ID 复用表清理）
	OnPreWorldInitializationHandle = FWorldDelegates::OnPreWorldInitialization.AddStatic(&INavLinkCustomInterface::OnPreWorldInitialization);
	// 引擎核心初始化完成后 —— 刷新 NavMeshBoundsVolume 的静态注册表
	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&ANavMeshBoundsVolume::OnPostEngineInit);
#endif
}

// ShutdownModule：模块卸载时反注册委托，避免悬空回调。
void FNavigationSystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);
	FWorldDelegates::OnPreWorldInitialization.Remove(OnPreWorldInitializationHandle);
#endif
}

// 历史草稿：曾经考虑由模块直接创建 NavigationSystem 实例，现已被 World 自动创建路径取代。
//UNavigationSystemBase* FNavigationSystemModule::CreateNavigationSystemInstance(UWorld& World)
//{
//	UE_LOG(LogNavSysModule, Log, TEXT("Creating NavigationSystem for world %s"), *World.GetName());
//	
//	/*TSubclassOf<UNavigationSystemBase> NavSystemClass = LoadClass<UNavigationSystemBase>(NULL, *UNavigationSystemBase::GetNavigationSystemClassName().ToString(), NULL, LOAD_None, NULL);
//	return NewObject<UNavigationSystemBase>(&World, NavSystemClass);*/
//	return nullptr;
//}

#undef LOCTEXT_NAMESPACE
