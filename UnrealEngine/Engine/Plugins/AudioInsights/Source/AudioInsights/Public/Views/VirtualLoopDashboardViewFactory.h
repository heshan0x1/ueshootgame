// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Views/TableDashboardViewFactory.h"

#define UE_API AUDIOINSIGHTS_API


namespace UE::Audio::Insights
{
	class FVirtualLoopDashboardViewFactory : public FTraceObjectTableDashboardViewFactory
	{
	public:
		UE_API FVirtualLoopDashboardViewFactory();
		virtual ~FVirtualLoopDashboardViewFactory() = default;

		UE_API virtual FName GetName() const override;
		UE_API virtual FText GetDisplayName() const override;
		UE_API virtual FSlateIcon GetIcon() const override;
		UE_API virtual EDefaultDashboardTabStack GetDefaultTabStack() const override;

#if WITH_EDITOR
		DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDebugDrawEntries, float /*InElapsed*/, const TArray<TSharedPtr<IDashboardDataViewEntry>>& /*InSelectedItems*/, ::Audio::FDeviceId /*InAudioDeviceId*/);
		UE_API inline static FOnDebugDrawEntries OnDebugDrawEntries;
#endif // WITH_EDITOR

	protected:
		UE_API virtual void ProcessEntries(FTraceTableDashboardViewFactory::EProcessReason Reason) override;
		UE_API virtual const TMap<FName, FTraceTableDashboardViewFactory::FColumnData>& GetColumns() const override;

		UE_API virtual void SortTable() override;

#if WITH_EDITOR
		UE_API virtual bool IsDebugDrawEnabled() const override;
		UE_API virtual void DebugDraw(float InElapsed, const TArray<TSharedPtr<IDashboardDataViewEntry>>& InSelectedItems, ::Audio::FDeviceId InAudioDeviceId) const;
#endif // WITH_EDITOR
	};
} // namespace UE::Audio::Insights

#undef UE_API
