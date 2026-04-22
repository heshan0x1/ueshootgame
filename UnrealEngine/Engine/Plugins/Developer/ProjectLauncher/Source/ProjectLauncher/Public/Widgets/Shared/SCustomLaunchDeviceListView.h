// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Shared/SCustomLaunchDeviceWidgetBase.h"

#define UE_API PROJECTLAUNCHER_API

class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;

class SCustomLaunchDeviceListView
	: public SCustomLaunchDeviceWidgetBase
{
public:
	SLATE_BEGIN_ARGS(SCustomLaunchDeviceListView)
		: _AllPlatforms(false)
		, _SingleSelect(false)
		{}
		SLATE_EVENT(FOnDeviceRemoved, OnDeviceRemoved)
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged);
		SLATE_ATTRIBUTE(TArray<FString>, SelectedDevices);
		SLATE_ATTRIBUTE(TArray<FString>, Platforms);
		SLATE_ARGUMENT(bool, AllPlatforms)
		SLATE_ARGUMENT(bool, SingleSelect)
	SLATE_END_ARGS()

public:
	UE_API void Construct(	const FArguments& InArgs );

protected:
	bool bSingleSelect;

	UE_API TSharedRef<ITableRow> GenerateDeviceProxyRow(TSharedPtr<ITargetDeviceProxy> DeviceProxy, const TSharedRef<STableViewBase>& OwnerTable);
	UE_API ECheckBoxState IsDeviceProxyChecked(TSharedPtr<ITargetDeviceProxy> DeviceProxy) const;
	UE_API void OnDeviceProxyCheckStateChanged(ECheckBoxState NewState, TSharedPtr<ITargetDeviceProxy> DeviceProxy);
	UE_API virtual void OnDeviceListRefreshed() override;

private:
	TSharedPtr<SListView<TSharedPtr<ITargetDeviceProxy>> > DeviceProxyListView;

};

#undef UE_API
