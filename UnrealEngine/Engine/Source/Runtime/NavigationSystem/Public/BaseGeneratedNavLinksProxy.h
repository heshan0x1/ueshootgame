// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// BaseGeneratedNavLinksProxy.h
// -----------------------------------------------------------------------------
// "自动生成链接" 的 Proxy 基类（Experimental）。
// 当 Recast 沿 Tile 边缘自动采样生成一批跳跃链接时，这批链接需要一个共享的 UObject
// 作为"Owner"，用于存 ID 与响应接口调用。这个 Proxy 就是此用途的基类。
//
// 要点：
//   - 一个 Proxy 实例对应一条"生成配置"，它产生的所有链接共用同一个 LinkProxyId。
//   - 实现了 INavLinkCustomInterface，但 GetLinkData 没有具体端点（所有自动生成的
//     链接端点由 Recast 自己记录在 dtOffMeshConnection 上）。
//   - 蓝图可派生：用户可以继承此类并在 Blueprint 中覆盖自定义链接行为。
// =============================================================================

#pragma once

#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NavLinkCustomInterface.h"

#include "BaseGeneratedNavLinksProxy.generated.h"

/**
 * Experimental
 * Base class used to create generated navlinks proxy.
 * The proxy id is used to represent multiple links generated from the same configuration.
 */
UCLASS(Blueprintable, MinimalAPI)
class UBaseGeneratedNavLinksProxy : public UObject, public INavLinkCustomInterface
{
	GENERATED_UCLASS_BODY()
	
	// BEGIN INavLinkCustomInterface
	// 基类没有具体端点；派生类可覆盖以提供链接端点信息。
	NAVIGATIONSYSTEM_API virtual void GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const override;
	// 返回共享的 LinkProxyId —— 由本 Proxy 生成的所有链接都以此为 ID。
	NAVIGATIONSYSTEM_API virtual FNavLinkId GetId() const override;
	// 更新 LinkProxyId（典型时机：在 NavigationSystem 批量重分配 ID 时）。
	NAVIGATIONSYSTEM_API virtual void UpdateLinkId(FNavLinkId ProxyId) override;
	// 返回 Owner，指向创建此 Proxy 的业务对象。
	NAVIGATIONSYSTEM_API virtual UObject* GetLinkOwner() const override;
	// END INavLinkCustomInterface

	// 运行时设置 Owner。
	void SetOwner(UObject* NewOwner) { Owner = NewOwner; }
	
protected:
	/** The LinkID will be the same for all navlinks using the proxy. */
	// Proxy 生成的所有链接共享此 ID；Transient 因为运行时分配 + 序列化另有机制。
	UPROPERTY(Transient)
	FNavLinkId LinkProxyId;

	/** Proxy owner. */
	// 业务 Owner（例如生成本 Proxy 的 Generator），用于链接回调反向查询。
	UPROPERTY(Transient)
	TObjectPtr<UObject> Owner = nullptr;
};
