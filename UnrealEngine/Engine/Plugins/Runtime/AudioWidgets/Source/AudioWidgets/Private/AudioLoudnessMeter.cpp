// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioLoudnessMeter.h"

#include "LoudnessMeterRackUnitSettings.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FAudioLoudnessMeter"

namespace AudioWidgets
{
	/**
	 * Light wrapper for accessing settings for analyzer rack units. Can be passed by value.
	 */
	template<class SettingsT>
	class TRackUnitSettingsHelper
	{
	public:
		TRackUnitSettingsHelper(const FProperty& InSettingsProperty)
			: SettingsProperty(InSettingsProperty)
		{
		}

		SettingsT& GetRackUnitSettings() const
		{
			UObject* EditorSettingsObject = GetEditorSettingsObject();
			return *(SettingsProperty.ContainerPtrToValuePtr<SettingsT>(EditorSettingsObject));
		}

		void SaveConfig() const
		{
			GetEditorSettingsObject()->SaveConfig();
		}

	private:
		UObject* GetEditorSettingsObject() const
		{
			return SettingsProperty.GetOwnerClass()->GetDefaultObject();
		}

		const FProperty& SettingsProperty;
	};

	const FAudioAnalyzerRackUnitTypeInfo FAudioLoudnessMeter::RackUnitTypeInfo
	{
		.TypeName = TEXT("FAudioLoudnessMeter"),
		.DisplayName = LOCTEXT("AudioLoudnessMeterDisplayName", "Loudness"),
		.OnMakeAudioAnalyzerRackUnit = FOnMakeAudioAnalyzerRackUnit::CreateStatic(&MakeRackUnit),
		.VerticalSizeCoefficient = 0.5f,
	};

	FAudioLoudnessMeter::FAudioLoudnessMeter(const FAudioLoudnessMeterParams& Params)
	{
		Init(Params.NumChannels, Params.AudioDeviceId, Params.ExternalAudioBus);
	}

	FAudioLoudnessMeter::~FAudioLoudnessMeter()
	{
		if (WidgetRefreshTicker.IsValid())
		{
			FTSTicker::RemoveTicker(WidgetRefreshTicker);
			WidgetRefreshTicker.Reset();
		}

		Teardown();
	}

	void FAudioLoudnessMeter::SetAudioBusInfo(const FAudioBusInfo& AudioBusInfo)
	{
		Init(AudioBusInfo.AudioBus->GetNumChannels(), AudioBusInfo.AudioDeviceId, AudioBusInfo.AudioBus);
	}

	TSharedRef<SDockTab> FAudioLoudnessMeter::SpawnTab(const FSpawnTabArgs& Args) const
	{
		return SNew(SDockTab)
			.Clipping(EWidgetClipping::ClipToBounds)
			.Label(RackUnitTypeInfo.DisplayName)
			[
				LoudnessMeterWidgetView.MakeWidget()
			];
	}

	TSharedRef<IAudioAnalyzerRackUnit> FAudioLoudnessMeter::MakeRackUnit(const FAudioAnalyzerRackUnitConstructParams& Params)
	{
		const FAudioLoudnessMeterParams AnalyzerParams
		{
			.NumChannels = Params.AudioBusInfo.GetNumChannels(),
			.AudioDeviceId = Params.AudioBusInfo.AudioDeviceId,
			.ExternalAudioBus = Params.AudioBusInfo.AudioBus,
		};

		TSharedRef<FAudioLoudnessMeter> AudioLoudnessMeter = MakeShared<FAudioLoudnessMeter>(AnalyzerParams);
		AudioLoudnessMeter->InitLoudnessMeterWidgetView(Params);

		return AudioLoudnessMeter;
	}

	void FAudioLoudnessMeter::Init(const int32 InNumChannels, const Audio::FDeviceId InAudioDeviceId, const TObjectPtr<UAudioBus> InExternalAudioBus)
	{
		Teardown();

		LKFSSettings = TStrongObjectPtr(NewObject<ULKFSSettings>());

		AudioDeviceId = InAudioDeviceId;

		// Only create analyzers etc if we have an audio device:
		if (InAudioDeviceId != FAudioBusInfo::InvalidAudioDeviceId)
		{
			check(InNumChannels > 0);

			bUseExternalAudioBus = (InExternalAudioBus != nullptr);
			AudioBus = (bUseExternalAudioBus) ? TStrongObjectPtr(InExternalAudioBus.Get()) : TStrongObjectPtr(NewObject<UAudioBus>());
			AudioBus->AudioBusChannels = EAudioBusChannels(InNumChannels - 1);

			CreateLKFSAnalyzer();

			LKFSAnalyzer->StartAnalyzing(InAudioDeviceId, AudioBus.Get());
		}
	}

	void FAudioLoudnessMeter::Teardown()
	{
		if (LKFSAnalyzer.IsValid() && LKFSAnalyzer->IsValidLowLevel())
		{
			LKFSAnalyzer->StopAnalyzing();
			ReleaseLKFSAnalyzer();
		}

		AudioBus.Reset();
		LKFSSettings.Reset();
		bUseExternalAudioBus = false;
	}

	void FAudioLoudnessMeter::CreateLKFSAnalyzer()
	{
		ensure(!LKFSAnalyzer.IsValid());
		ensure(!LatestOverallLKFSResultsDelegateHandle.IsValid());

		LKFSAnalyzer = TStrongObjectPtr(NewObject<ULKFSAnalyzer>());
		LKFSAnalyzer->Settings = LKFSSettings.Get();
		LatestOverallLKFSResultsDelegateHandle = LKFSAnalyzer->OnLatestOverallLKFSResultsNative.AddRaw(this, &FAudioLoudnessMeter::OnLoudnessOutput);
	}

	void FAudioLoudnessMeter::ReleaseLKFSAnalyzer()
	{
		if (ensure(LKFSAnalyzer.IsValid() && LatestOverallLKFSResultsDelegateHandle.IsValid()))
		{
			LKFSAnalyzer->OnLatestOverallLKFSResultsNative.Remove(LatestOverallLKFSResultsDelegateHandle);
		}

		LatestOverallLKFSResultsDelegateHandle.Reset();
		LKFSAnalyzer.Reset();
	}

	void FAudioLoudnessMeter::InitLoudnessMeterWidgetView(const FAudioAnalyzerRackUnitConstructParams& Params)
	{
		// Initializing these callbacks outside of the FAudioLoudnessMeter constructor allows us to use the CreateSP(...) smart pointer variants for attributes/delegates:

		FLoudnessMeterWidgetView::FTimerPanelParams TimerPanelParams
		{
			.AnalysisTime = TAttribute<FTimespan>::CreateSP(this, &FAudioLoudnessMeter::GetAnalysisTime),
			.OnResetButtonClicked = FOnClicked::CreateSP(this, &FAudioLoudnessMeter::HandleResetButtonClicked),
			.bIsVisible = true
		};

		FLoudnessMeterWidgetView::FLoudnessMetric LoudnessMetrics[] =
		{
			{
				.Name = GET_MEMBER_NAME_CHECKED(FLoudnessMeterRackUnitSettings, LongTermLoudness),
				.DisplayName = LOCTEXT("LongTermLoudnessDisplayName", "Long Term"),
				.Value = TAttribute<TOptional<float>>::CreateSPLambda(this, [this]() { return (LatestLoudnessResults.IsSet()) ? TOptional<float>(LatestLoudnessResults->GatedLoudness) : NullOpt; }),
				.bShowValue = true,
				.bShowMeter = false,
			},
			{
				.Name = GET_MEMBER_NAME_CHECKED(FLoudnessMeterRackUnitSettings, ShortTermLoudness),
				.DisplayName = LOCTEXT("ShortTermLoudnessDisplayName", "Short Term"),
				.Value = TAttribute<TOptional<float>>::CreateSPLambda(this, [this]() { return (LatestLoudnessResults.IsSet()) ? TOptional<float>(LatestLoudnessResults->ShortTermLoudness) : NullOpt; }),
				.bShowValue = false,
				.bShowMeter = true,
			},
			{
				.Name = GET_MEMBER_NAME_CHECKED(FLoudnessMeterRackUnitSettings, MomentaryLoudness),
				.DisplayName = LOCTEXT("MomentaryLoudnessDisplayName", "Momentary"),
				.Value = TAttribute<TOptional<float>>::CreateSPLambda(this, [this]() { return (LatestLoudnessResults.IsSet()) ? TOptional<float>(LatestLoudnessResults->Loudness) : NullOpt; }),
				.bShowValue = false,
				.bShowMeter = true,
			}
		};

		if (Params.EditorSettingsClass != nullptr)
		{
			// If we have been given a valid editor settings class, bind analyzer options to the settings:
			if (const FProperty* LoudnessMeterSettingsProperty = Params.EditorSettingsClass->FindPropertyByName("LoudnessMeterSettings"))
			{
				const TRackUnitSettingsHelper<FLoudnessMeterRackUnitSettings> SettingsHelper(*LoudnessMeterSettingsProperty);

				TimerPanelParams.bIsVisible.BindLambda([=]() { return SettingsHelper.GetRackUnitSettings().bDisplayAnalysisTimer; });
				TimerPanelParams.OnVisibilityToggleRequested.BindLambda([=]()
					{
						FLoudnessMeterRackUnitSettings& LoudnessMeterSettings = SettingsHelper.GetRackUnitSettings();
						LoudnessMeterSettings.bDisplayAnalysisTimer = !LoudnessMeterSettings.bDisplayAnalysisTimer;
						SettingsHelper.SaveConfig();
					});

				for (FLoudnessMeterWidgetView::FLoudnessMetric& LoudnessMetric : LoudnessMetrics)
				{
					if (const FProperty* DisplayOptionsProperty = FLoudnessMeterRackUnitSettings::StaticStruct()->FindPropertyByName(LoudnessMetric.Name))
					{
						LoudnessMetric.bShowValue.BindLambda([=]()
							{
								const FLoudnessMetricDisplayOptions* DisplayOptions = DisplayOptionsProperty->ContainerPtrToValuePtr<FLoudnessMetricDisplayOptions>(&SettingsHelper.GetRackUnitSettings());
								return DisplayOptions->bShowValue;
							});

						LoudnessMetric.bShowMeter.BindLambda([=]()
							{
								const FLoudnessMetricDisplayOptions* DisplayOptions = DisplayOptionsProperty->ContainerPtrToValuePtr<FLoudnessMetricDisplayOptions>(&SettingsHelper.GetRackUnitSettings());
								return DisplayOptions->bShowMeter;
							});

						LoudnessMetric.OnShowValueToggleRequested.BindLambda([=]()
							{
								FLoudnessMetricDisplayOptions* DisplayOptions = DisplayOptionsProperty->ContainerPtrToValuePtr<FLoudnessMetricDisplayOptions>(&SettingsHelper.GetRackUnitSettings());
								DisplayOptions->bShowValue = !DisplayOptions->bShowValue;
								SettingsHelper.SaveConfig();
							});

						LoudnessMetric.OnShowMeterToggleRequested.BindLambda([=]()
							{
								FLoudnessMetricDisplayOptions* DisplayOptions = DisplayOptionsProperty->ContainerPtrToValuePtr<FLoudnessMetricDisplayOptions>(&SettingsHelper.GetRackUnitSettings());
								DisplayOptions->bShowMeter = !DisplayOptions->bShowMeter;
								SettingsHelper.SaveConfig();
							});
					}
				}

				// We just poll for visible loudness metrics:
				WidgetRefreshTicker = FTSTicker::GetCoreTicker().AddTicker(TEXT("FAudioLoudnessMeter::WidgetRefreshTicker"), 0.0f, [this](float DeltaTime)
					{
						LoudnessMeterWidgetView.RefreshVisibleLoudnessMetrics();
						return true;
					});
			}
		}

		LoudnessMeterWidgetView.InitTimerPanel(TimerPanelParams);
		for (const FLoudnessMeterWidgetView::FLoudnessMetric& LoudnessMetric : LoudnessMetrics)
		{
			LoudnessMeterWidgetView.AddLoudnessMetric(LoudnessMetric);
		}
	}

	void FAudioLoudnessMeter::OnLoudnessOutput(ULKFSAnalyzer* InLKFSAnalyzer, const FLKFSResults& InLoudnessResults)
	{
		if (InLKFSAnalyzer == LKFSAnalyzer.Get())
		{
			LatestLoudnessResults = InLoudnessResults;
		}
	}

	FTimespan FAudioLoudnessMeter::GetAnalysisTime() const
	{
		if (LatestLoudnessResults.IsSet() && LKFSSettings.IsValid())
		{
			const float AnalysisTime = FMath::Min(LatestLoudnessResults->Timestamp, LKFSSettings->IntegratedLoudnessDuration);
			return FTimespan::FromSeconds(AnalysisTime);
		}
	
		return FTimespan::Zero();
	}

	FReply FAudioLoudnessMeter::HandleResetButtonClicked()
	{
		if (LKFSAnalyzer.IsValid())
		{
			LKFSAnalyzer->StopAnalyzing();
			ReleaseLKFSAnalyzer();
		}

		LatestLoudnessResults.Reset();

		if (AudioDeviceId != FAudioBusInfo::InvalidAudioDeviceId)
		{
			CreateLKFSAnalyzer();
			LKFSAnalyzer->StartAnalyzing(AudioDeviceId, AudioBus.Get());
		}

		return FReply::Handled();
	}
} // namespace AudioWidgets

#undef LOCTEXT_NAMESPACE
