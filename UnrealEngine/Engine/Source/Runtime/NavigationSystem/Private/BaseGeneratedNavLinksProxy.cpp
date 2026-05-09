// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// BaseGeneratedNavLinksProxy.cpp
// -----------------------------------------------------------------------------
// UBaseGeneratedNavLinksProxy 实现：为一组"自动生成的链接"提供共享 Owner + ID。
// 关键点：它不代表单一链接，所以 GetLinkData 返回的是断言（不应被调用）。
// =============================================================================

#include "BaseGeneratedNavLinksProxy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BaseGeneratedNavLinksProxy)

UBaseGeneratedNavLinksProxy::UBaseGeneratedNavLinksProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// Proxy 代表"多条自动生成链接"，没有单一端点。调用到这里通常意味着调用方搞错了对象。
void UBaseGeneratedNavLinksProxy::GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const
{
	ensureMsgf(false, TEXT("Should not be called on a generated navlink proxy since it's not representing a single link."));
}

// 返回共享 ID——所有由本 Proxy 生成的链接都用同一 ID 识别
FNavLinkId UBaseGeneratedNavLinksProxy::GetId() const
{
	return LinkProxyId;
}

// 批量重分配时（如 World 载入后 ID 迁移）更新本 Proxy 的 ID
void UBaseGeneratedNavLinksProxy::UpdateLinkId(FNavLinkId ProxyId)
{
	LinkProxyId = ProxyId;
}

UObject* UBaseGeneratedNavLinksProxy::GetLinkOwner() const
{
	return Owner;
}
