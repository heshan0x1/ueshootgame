// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioMeterChannelInfo.h"
#include "Containers/Ticker.h"
#include "Providers/SubmixTraceProvider.h"
#include "Views/SubmixDashboardViewFactory.h"

namespace UE::Audio::Insights
{
	class FOutputMeterDashboardViewFactory : public IDashboardViewFactory, public TSharedFromThis<FOutputMeterDashboardViewFactory>
	{
	public:
		FOutputMeterDashboardViewFactory(TSharedRef<FSubmixDashboardViewFactory> SubmixDashboard);
		virtual ~FOutputMeterDashboardViewFactory();

		virtual EDefaultDashboardTabStack GetDefaultTabStack() const override;
		virtual FText GetDisplayName() const override;
		virtual FName GetName() const override;
		virtual FSlateIcon GetIcon() const override;
		virtual TSharedRef<SWidget> MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs) override;

	private:
		void Tick(float InElapsed);
		void HandleSessionInstanceUpdated();
		void ProcessDeviceData(const FSubmixTraceProvider::FDeviceData& DeviceData);
		TArray<FAudioMeterChannelInfo> GetAudioMeterChannelInfo() const;

		TSharedPtr<const FSubmixTraceProvider> SubmixProvider;
		FTSTicker::FDelegateHandle TickerHandle;
		FDelegateHandle SessionInstanceUpdatedDelegateHandle;
		TOptional<uint64> ProcessedUpdateId;
		TSharedPtr<FSubmixDashboardEntry> SubmixDashboardEntry;
	};
} // namespace UE::Audio::Insights

