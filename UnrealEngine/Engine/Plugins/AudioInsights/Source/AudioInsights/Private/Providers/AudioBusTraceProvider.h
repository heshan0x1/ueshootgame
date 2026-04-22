// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioInsightsTraceProviderBase.h"
#include "Messages/AudioBusTraceMessages.h"
#include "UObject/NameTypes.h"

#if WITH_EDITOR
#include "Delegates/Delegate.h"
#include "Providers/AssetProvider.h"
#endif // WITH_EDITOR

namespace UE::Audio::Insights
{
	class FAudioBusTraceProvider : public TDeviceDataMapTraceProvider<uint32, TSharedPtr<FAudioBusDashboardEntry>>, public TSharedFromThis<FAudioBusTraceProvider>
	{
	public:
		FAudioBusTraceProvider();
		virtual ~FAudioBusTraceProvider();

		static FName GetName_Static();
		
		virtual UE::Trace::IAnalyzer* ConstructAnalyzer(TraceServices::IAnalysisSession& InSession) override;

#if !WITH_EDITOR
		virtual void InitSessionCachedMessages(TraceServices::IAnalysisSession& InSession) override;
#endif // !WITH_EDITOR
		void RequestEntriesUpdate();

		DECLARE_DELEGATE_OneParam(FOnAudioBusChanged, const uint32 /*AudioBusId*/);
		FOnAudioBusChanged OnAudioBusAdded;
		FOnAudioBusChanged OnAudioBusRemoved;
		FOnAudioBusChanged OnAudioBusStarted;

		DECLARE_DELEGATE(FOnAudioBusListUpdated);
		FOnAudioBusListUpdated OnAudioBusListUpdated;

		DECLARE_DELEGATE(FOnTimeMarkerUpdated);
		FOnTimeMarkerUpdated OnTimeMarkerUpdated;

	protected:
		virtual void OnTraceChannelsEnabled() override;

	private:
		virtual bool ProcessMessages() override;

#if WITH_EDITOR
		virtual void OnTimeControlMethodReset() override;

		void HandleOnAudioBusAssetAdded(const FString& InAssetPath);
		void HandleOnAudioBusAssetRemoved(const FString& InAssetPath);
		void HandleOnAudioBusAssetListUpdated(const TArray<FString>& InAssetPaths);
#else
		TUniquePtr<FAudioBusSessionCachedMessages> SessionCachedMessages;
#endif // WITH_EDITOR

		virtual void OnTimingViewTimeMarkerChanged(double TimeMarker) override;

		FAudioBusMessages TraceMessages;

#if WITH_EDITOR
		TAssetProvider<UAudioBus> AudioBusAssetProvider;
		bool bAssetsUpdated = false;
#endif // WITH_EDITOR
	};
} // namespace UE::Audio::Insights
