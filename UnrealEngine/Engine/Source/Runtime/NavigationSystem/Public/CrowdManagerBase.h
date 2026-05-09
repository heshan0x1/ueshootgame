// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 UCrowdManagerBase —— Crowd Manager（人群避障管理器）的抽象基类。
//   NavigationSystem 只暴露基类插槽，真正的实现（基于 dtCrowd 的避障、预测、
//   分离等）位于 AIModule 的 UCrowdManager。
//
// 使用方式：
//   - 在 UNavigationSystemV1::CrowdManagerClass 指定派生类即可替换默认实现；
//   - NavigationSystem 会在 NavData 注册/注销、每帧 Tick 时回调本接口。
// =============================================================================

#pragma once

#include "CrowdManagerBase.generated.h"

class ANavigationData;

/** Base class for Crowd Managers. If you want to create a custom crowd manager
 *	implement a class extending this one and set UNavigationSystemV1::CrowdManagerClass
 *	to point at your class */
// Crowd Manager 抽象基类，实际实现在 AIModule；此处仅作为 NavigationSystem 的插槽。
UCLASS(Abstract, Transient, MinimalAPI)
class UCrowdManagerBase : public UObject
{
	GENERATED_BODY()
public:
	// 每帧由 NavigationSystem 驱动 Tick（用于推进人群仿真状态）。
	virtual void Tick(float DeltaTime) PURE_VIRTUAL(UCrowdManagerBase::Tick, );

	/** Called by the nav system when a new navigation data instance is registered. */
	// 新 NavData 注册时回调，派生类可据此创建 dtCrowd 或更新 Agent 映射。
	virtual void OnNavDataRegistered(ANavigationData& NavDataInstance) PURE_VIRTUAL(UCrowdManagerBase::OnNavDataRegistered, );

	/** Called by the nav system when a navigation data instance is removed. */
	// NavData 注销时回调，清理与该 NavData 关联的仿真资源。
	virtual void OnNavDataUnregistered(ANavigationData& NavDataInstance) PURE_VIRTUAL(UCrowdManagerBase::OnNavDataUnregistered, );

	// 清理挂起状态（World 切换/关闭时调用）。
	virtual void CleanUp(float DeltaTime) PURE_VIRTUAL(UCrowdManagerBase::CleanUp, );
};

