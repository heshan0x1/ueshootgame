// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDetailsNavigableWidgetRegistrar.h"

#include "AnimDetailsProxyManager.h"

namespace UE::ControlRigEditor
{
	FAnimDetailsNavigableWidgetRegistrar::~FAnimDetailsNavigableWidgetRegistrar()
	{
		if (!IsEngineExitRequested() &&
			WeakProxyManager.IsValid() &&
			WeakOwnerWidget.IsValid())
		{
			WeakProxyManager->GetNavigableWidgetRegistry().UnregisterNavigableWidget(WeakOwnerWidget.Pin().ToSharedRef());
			WeakProxyManager->GetNavigableWidgetRegistry().UnregisterNavigatorWidget(WeakOwnerWidget.Pin().ToSharedRef());
		}
	}

	void FAnimDetailsNavigableWidgetRegistrar::RegisterAsNavigable(
		UAnimDetailsProxyManager& ProxyManager, 
		const TSharedRef<SWidget>& OwnerWidget,
		const TSharedRef<SWidget>& NavigableWidget)
	{
		if (WeakProxyManager.IsValid() &&
			WeakOwnerWidget.IsValid())
		{
			WeakProxyManager->GetNavigableWidgetRegistry().UnregisterNavigableWidget(WeakOwnerWidget.Pin().ToSharedRef());
		}

		ProxyManager.GetNavigableWidgetRegistry().RegisterNavigableWidget(OwnerWidget, NavigableWidget);

		WeakProxyManager = &ProxyManager;
		WeakOwnerWidget = NavigableWidget;
	}

	void FAnimDetailsNavigableWidgetRegistrar::RegisterAsNavigator(
		UAnimDetailsProxyManager& ProxyManager, 
		const TSharedRef<SWidget>& NavigatorWidget, 
		const TSharedRef<SWidget>& NavigateToOwner)
	{
		if (WeakProxyManager.IsValid() &&
			WeakOwnerWidget.IsValid())
		{
			WeakProxyManager->GetNavigableWidgetRegistry().UnregisterNavigatorWidget(WeakOwnerWidget.Pin().ToSharedRef());
		}

		ProxyManager.GetNavigableWidgetRegistry().RegisterNavigatorWidget(NavigatorWidget, NavigateToOwner);

		WeakProxyManager = &ProxyManager;
		WeakOwnerWidget = NavigatorWidget;
	}
}
