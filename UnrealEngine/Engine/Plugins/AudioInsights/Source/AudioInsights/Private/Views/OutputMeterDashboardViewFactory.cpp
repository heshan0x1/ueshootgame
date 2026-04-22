// Copyright Epic Games, Inc. All Rights Reserved.
#include "Views/OutputMeterDashboardViewFactory.h"

#include "AudioDefines.h"
#include "AudioInsightsStyle.h"
#include "DSP/Dsp.h"
#include "ISessionServicesModule.h"
#include "SAudioMeterWidget.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "AudioInsights"

namespace UE::Audio::Insights
{
	FOutputMeterDashboardViewFactory::FOutputMeterDashboardViewFactory(TSharedRef<FSubmixDashboardViewFactory> SubmixDashboard)
		: SubmixProvider(SubmixDashboard->FindProvider<FSubmixTraceProvider>())
	{
	}

	FOutputMeterDashboardViewFactory::~FOutputMeterDashboardViewFactory()
	{
		if (TickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(TickerHandle);
		}
	}

	EDefaultDashboardTabStack FOutputMeterDashboardViewFactory::GetDefaultTabStack() const
	{
		return EDefaultDashboardTabStack::AudioAnalyzerRack;
	}

	FText FOutputMeterDashboardViewFactory::GetDisplayName() const
	{
		return LOCTEXT("AudioDashboard_DashboardsAnalyzerRackTab_DisplayName", "Analyzers");
	}

	FName FOutputMeterDashboardViewFactory::GetName() const
	{
		return "OutputMeter";
	}

	FSlateIcon FOutputMeterDashboardViewFactory::GetIcon() const
	{
		return FSlateStyle::Get().CreateIcon("AudioInsights.Icon");
	}

	TSharedRef<SWidget> FOutputMeterDashboardViewFactory::MakeWidget(TSharedRef<SDockTab> OwnerTab, const FSpawnTabArgs& SpawnTabArgs)
	{
		if (!TickerHandle.IsValid())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("FOutputMeterDashboardViewFactory"), 0.0f, [this](float DeltaTime)
				{
					Tick(DeltaTime);
					return true;
				});
		}

		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SColorBlock)
					.Color(FSlateStyle::Get().GetColor("AudioInsights.Analyzers.BackgroundColor"))
			]
			+ SOverlay::Slot()
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(SAudioMeterWidget)
							.Orientation(EOrientation::Orient_Vertical)
							.BackgroundColor(FLinearColor::Transparent)
							.MeterBackgroundColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterBackgroundColor)
							.MeterValueColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterValueColor)
							.MeterPeakColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterPeakColor)
							.MeterClippingColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterClippingColor)
							.MeterScaleColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterScaleColor)
							.MeterScaleLabelColor(FAudioMeterDefaultColorWidgetStyle::GetDefault().MeterScaleLabelColor)
							.MeterChannelInfo(TAttribute<TArray<FAudioMeterChannelInfo>>::CreateSP(this, &FOutputMeterDashboardViewFactory::GetAudioMeterChannelInfo))
					]
			];
	}

	void FOutputMeterDashboardViewFactory::Tick(float InElapsed)
	{
		// Try and subscribe to OnSessionInstanceUpdated events if not already:
		if (!SessionInstanceUpdatedDelegateHandle.IsValid())
		{
			ISessionServicesModule& SessionServicesModule = FModuleManager::LoadModuleChecked<ISessionServicesModule>("SessionServices");
			if (TSharedPtr<ISessionManager> SessionManager = SessionServicesModule.GetSessionManager())
			{
				SessionInstanceUpdatedDelegateHandle = SessionManager->OnSessionInstanceUpdated().AddSP(this, &FOutputMeterDashboardViewFactory::HandleSessionInstanceUpdated);
			}
		}

		if (SubmixProvider.IsValid())
		{
			const uint64 LastUpdateId = SubmixProvider->GetLastUpdateId();
			if (ProcessedUpdateId != LastUpdateId)
			{
				if (const FSubmixTraceProvider::FDeviceData* DeviceData = SubmixProvider->FindFilteredDeviceData())
				{
					ProcessDeviceData(*DeviceData);
				}

				ProcessedUpdateId = LastUpdateId;
			}
		}
	}

	void FOutputMeterDashboardViewFactory::HandleSessionInstanceUpdated()
	{
		if (SubmixDashboardEntry.IsValid())
		{
			// As we may have just gotten a valid session, we request starting of the envelope follower:
			FSubmixDashboardViewFactory::SendSubmixEnvelopeFollowerCVar(true, *SubmixDashboardEntry);
		}
	}

	void FOutputMeterDashboardViewFactory::ProcessDeviceData(const FSubmixTraceProvider::FDeviceData& DeviceData)
	{
		// Try and find the FSubmixDashboardEntry for the main submix:
		const auto IsMainSubmixEntryPair = [](const FSubmixTraceProvider::FEntryPair& EntryPair) { return EntryPair.Value->IsMainSubmix(); };
		const FSubmixTraceProvider::FEntryPair* MainSubmixEntryPair = Algo::FindByPredicate(DeviceData, IsMainSubmixEntryPair);
		TSharedPtr<FSubmixDashboardEntry> MainSubmixDashboardEntry = (MainSubmixEntryPair) ? MainSubmixEntryPair->Value : nullptr;

		if (SubmixDashboardEntry != MainSubmixDashboardEntry)
		{
			SubmixDashboardEntry = MainSubmixDashboardEntry;

			if (SubmixDashboardEntry.IsValid())
			{
				// Request starting of envelope follower:
				FSubmixDashboardViewFactory::SendSubmixEnvelopeFollowerCVar(true, *SubmixDashboardEntry);
			}
		}
	}

	TArray<FAudioMeterChannelInfo> FOutputMeterDashboardViewFactory::GetAudioMeterChannelInfo() const
	{
		TArray<FAudioMeterChannelInfo> MeterChannelInfo;

		if (SubmixDashboardEntry.IsValid())
		{
			const FAudioMeterInfo& AudioMeterInfo = *SubmixDashboardEntry->AudioMeterInfo;
			const TArray<float>& EnvelopeValues = AudioMeterInfo.EnvelopeValues;

			if (!EnvelopeValues.IsEmpty())
			{
				MeterChannelInfo.Reserve(EnvelopeValues.Num());
				for (const float EnvelopeValue : EnvelopeValues)
				{
					MeterChannelInfo.Emplace(::Audio::ConvertToDecibels(EnvelopeValue), MIN_VOLUME_DECIBELS, MIN_VOLUME_DECIBELS);
				}
			}
			else
			{
				MeterChannelInfo.Reserve(AudioMeterInfo.NumChannels);
				while (MeterChannelInfo.Num() < AudioMeterInfo.NumChannels)
				{
					MeterChannelInfo.Emplace(MIN_VOLUME_DECIBELS, MIN_VOLUME_DECIBELS, MIN_VOLUME_DECIBELS);
				}
			}
		}

		return MeterChannelInfo;
	}
} // namespace UE::Audio::Insights

#undef LOCTEXT_NAMESPACE
