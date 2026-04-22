// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2RTCModule.h"

#include "CoreMinimal.h"

#include "EpicRtcAllocator.h"
#include "EpicRtcAudioCapturer.h"
#include "EpicRtcLogging.h"
#include "EpicRtcVideoEncoderInitializer.h"
#include "EpicRtcVideoDecoderInitializer.h"
#include "EpicRtcWebsocketFactory.h"
#include "IMediaModule.h"
#include "IPixelStreaming2Module.h"
#include "Logging.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PixelStreaming2Delegates.h"
#include "PixelStreaming2PluginSettings.h"
#include "PixelStreaming2RTCPlayer.h"
#include "PixelStreaming2Utils.h"
#include "RendererInterface.h"
#include "Stats.h"
#include "UtilsCommon.h"
#include "UtilsCoder.h"
#include "UtilsCore.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"
#include "Video/Encoders/Configs/VideoEncoderConfigAV1.h"
#include "WebSocketsModule.h"

namespace UE::PixelStreaming2
{
	FPixelStreaming2RTCModule* FPixelStreaming2RTCModule::PixelStreaming2Module = nullptr;

	FUtf8String FPixelStreaming2RTCModule::EpicRtcConferenceName("pixel_streaming_conference_instance");

	/**
	 * Stats logger - as turned on/off by CVarPixelStreaming2LogStats
	 */
	void ConsumeStat(FString PlayerId, FName StatName, float StatValue)
	{
		UE_LOGFMT(LogPixelStreaming2RTC, Log, "[{0}]({1}) = {2}", PlayerId, StatName.ToString(), StatValue);
	}

	/**
	 * IModuleInterface implementation
	 */
	void FPixelStreaming2RTCModule::StartupModule()
	{
#if UE_SERVER
		// Hack to no-op the rest of the module so Blueprints can still work
		return;
#else
		if (!IsStreamingSupported())
		{
			return;
		}

		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		const ERHIInterfaceType RHIType = GDynamicRHI ? RHIGetInterfaceType() : ERHIInterfaceType::Hidden;
		// only D3D11/D3D12/Vulkan is supported
		if (!(RHIType == ERHIInterfaceType::D3D11 || RHIType == ERHIInterfaceType::D3D12 || RHIType == ERHIInterfaceType::Vulkan || RHIType == ERHIInterfaceType::Metal))
		{
#if !WITH_DEV_AUTOMATION_TESTS
			UE_LOG(LogPixelStreaming2RTC, Warning, TEXT("Only D3D11/D3D12/Vulkan/Metal Dynamic RHI is supported. Detected %s"), GDynamicRHI != nullptr ? GDynamicRHI->GetName() : TEXT("[null]"));
#endif
			return;
		}

		FString LogFilterString = UPixelStreaming2PluginSettings::CVarEpicRtcLogFilter.GetValueOnAnyThread() + TEXT("//\\bConference::Tick. Ticking audio (?:too|to) late\\b");
		UPixelStreaming2PluginSettings::CVarEpicRtcLogFilter->Set(*LogFilterString, ECVF_SetByHotfix);

		// By calling InitDefaultStreamer post engine init we can use pixel streaming in standalone editor mode
		IPixelStreaming2Module::Get().OnReady().AddLambda([this](IPixelStreaming2Module& CoreModule) {
			// Need to initialize after other modules have initialized such as NVCodec.
			if (!InitializeEpicRtc())
			{
				return;
			}

			if (!ensure(GEngine != nullptr))
			{
				return;
			}

			StreamerFactory.Reset(new FRTCStreamerFactory(EpicRtcConference));

			// Ensure we have ImageWrapper loaded, used in Freezeframes
			verify(FModuleManager::Get().LoadModule(FName("ImageWrapper")));

			bModuleReady = true;
			ReadyEvent.Broadcast(*this);
		});

		FModuleManager::LoadModuleChecked<FWebSocketsModule>("WebSockets");

		// Call these to initialize their singletons
		FStats::Get();

		if (UPixelStreaming2PluginSettings::FDelegates* Delegates = UPixelStreaming2PluginSettings::Delegates())
		{
			Delegates->OnLogStatsChanged.AddLambda([this](IConsoleVariable* Var) {
				bool					   bLogStats = Var->GetBool();
				UPixelStreaming2Delegates* Delegates = UPixelStreaming2Delegates::Get();
				if (!Delegates)
				{
					return;
				}
				if (bLogStats)
				{
					LogStatsHandle = Delegates->OnStatChangedNative.AddStatic(&ConsumeStat);
				}
				else
				{
					Delegates->OnStatChangedNative.Remove(LogStatsHandle);
				}
			});

			Delegates->OnWebRTCFpsChanged.AddLambda([](IConsoleVariable*) {
				IPixelStreaming2Module::Get().ForEachStreamer([](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					Streamer->RefreshStreamBitrate();
				});
			});

			Delegates->OnWebRTCBitrateChanged.AddLambda([](IConsoleVariable*) {
				IPixelStreaming2Module::Get().ForEachStreamer([](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					Streamer->RefreshStreamBitrate();
				});
			});
			Delegates->OnWebRTCDisableStatsChanged.AddLambda([this](IConsoleVariable* Var) {
				TRefCountPtr<EpicRtcConferenceInterface> Conference(EpicRtcConference);
				if (Conference)
				{
					if (Var->GetBool())
					{
						Conference->DisableStats();
					}
					else
					{
						Conference->EnableStats();
					}
				}
			});
		}

		if (IMediaModule* MediaModulePtr = FModuleManager::LoadModulePtr<IMediaModule>("Media"); MediaModulePtr != nullptr)
		{
			MediaModulePtr->RegisterPlayerFactory(*this);
		}

		bStartupCompleted = true;
#endif // UE_SERVER
	}

	void FPixelStreaming2RTCModule::ShutdownModule()
	{
		if (!IsStreamingSupported())
		{
			return;
		}

		if (!bStartupCompleted)
		{
			return;
		}

		if (IMediaModule* MediaModulePtr = FModuleManager::LoadModulePtr<IMediaModule>("Media"); MediaModulePtr != nullptr)
		{
			MediaModulePtr->UnregisterPlayerFactory(*this);
		}

		TickableTasks.Reset();
		StreamerFactory.Reset();

		if (!EpicRtcPlatform)
		{
			UE_LOGFMT(LogPixelStreaming2RTC, Error, "EpicRtcPlatform does not exist during shutdown when it is expected to exist");
		}
		else
		{
			EpicRtcPlatform->ReleaseConference(ToEpicRtcStringView(EpicRtcConferenceName));
		}

		bStartupCompleted = false;
	}

	/**
	 * End IModuleInterface implementation
	 */

	FPixelStreaming2RTCModule* FPixelStreaming2RTCModule::GetModule()
	{
		if (!PixelStreaming2Module)
		{
			PixelStreaming2Module = FModuleManager::Get().LoadModulePtr<FPixelStreaming2RTCModule>("PixelStreaming2RTC");
		}

		return PixelStreaming2Module;
	}

	/**
	 * IPixelStreaming2RTCModule implementation
	 */
	IPixelStreaming2RTCModule::FReadyEvent& FPixelStreaming2RTCModule::OnReady()
	{
		return ReadyEvent;
	}

	bool FPixelStreaming2RTCModule::IsReady()
	{
		return bModuleReady;
	}
	/**
	 * End IPixelStreaming2RTCModule implementation
	 */

	/**
	 * IMediaPlayerFactory implementation
	 */
	TSharedPtr<IMediaPlayer> FPixelStreaming2RTCModule::CreatePlayer(IMediaEventSink& EventSink)
	{
		return MakeShared<FPixelStreaming2RTCStreamPlayer>();
	}

	FName FPixelStreaming2RTCModule::GetPlayerName() const
	{
		return "PixelStreaming2RTC";
	}

	FText FPixelStreaming2RTCModule::GetDisplayName() const
	{
		return NSLOCTEXT("PixelStreaming2", "MediaPlayerFactory", "PixelStreaming2 RTC Stream Player");
	}

	bool FPixelStreaming2RTCModule::CanPlayUrl(const FString& Url, const IMediaOptions* Options, TArray<FText>* OutWarnings, TArray<FText>* OutErrors) const
	{
		return Url.StartsWith(TEXT("ws://")) || Url.StartsWith(TEXT("wss://"));
	}

	bool FPixelStreaming2RTCModule::SupportsFeature(EMediaFeature Feature) const
	{
		return Feature == EMediaFeature::VideoSamples || Feature == EMediaFeature::AudioSamples;
	}

	FGuid FPixelStreaming2RTCModule::GetPlayerPluginGUID() const
	{
		static FGuid PlayerPluginGUID = FGuid::NewGuid();
		return PlayerPluginGUID;
	}

	const TArray<FString>& FPixelStreaming2RTCModule::GetSupportedPlatforms() const
	{
		static TArray<FString> SupportedPlatforms = { TEXT("Windows"), TEXT("Linux"), TEXT("Mac") };
		return SupportedPlatforms;
	}
	/**
	 * End IMediaPlayerFactory implementation
	 */

	TSharedPtr<FSharedTickableTasks> FPixelStreaming2RTCModule::GetSharedTickableTasks()
	{
		TSharedPtr<FSharedTickableTasks> PinnedTickableTasks = TickableTasks.Pin();
		if (!PinnedTickableTasks)
		{
			PinnedTickableTasks = MakeShared<FSharedTickableTasks>(FPixelStreamingTickableTask::Create<FEpicRtcTickConferenceTask>(EpicRtcConference, TEXT("PixelStreaming2Module TickConferenceTask")));
			TickableTasks = PinnedTickableTasks;
		}
		return PinnedTickableTasks;
	}

	FString FPixelStreaming2RTCModule::GetFieldTrials()
	{
		FString FieldTrials = UPixelStreaming2PluginSettings::CVarWebRTCFieldTrials.GetValueOnAnyThread();

		// Set the WebRTC-FrameDropper/Disabled/ if the CVar is set
		if (UPixelStreaming2PluginSettings::CVarWebRTCDisableFrameDropper.GetValueOnAnyThread())
		{
			FieldTrials += TEXT("WebRTC-FrameDropper/Disabled/");
		}

		if (UPixelStreaming2PluginSettings::CVarWebRTCEnableFlexFec.GetValueOnAnyThread())
		{
			FieldTrials += TEXT("WebRTC-FlexFEC-03-Advertised/Enabled/WebRTC-FlexFEC-03/Enabled/");
		}

		// Parse "WebRTC-Video-Pacing/" field trial
		{
			float PacingFactor = UPixelStreaming2PluginSettings::CVarWebRTCVideoPacingFactor.GetValueOnAnyThread();
			float PacingMaxDelayMs = UPixelStreaming2PluginSettings::CVarWebRTCVideoPacingMaxDelay.GetValueOnAnyThread();

			if (PacingFactor >= 0.0f || PacingMaxDelayMs >= 0.0f)
			{
				FString VideoPacingFieldTrialStr = TEXT("WebRTC-Video-Pacing/");
				bool	bHasPacingFactor = PacingFactor >= 0.0f;
				if (bHasPacingFactor)
				{
					VideoPacingFieldTrialStr += FString::Printf(TEXT("factor:%.1f"), PacingFactor);
				}
				bool bHasMaxDelay = PacingMaxDelayMs >= 0.0f;
				if (bHasMaxDelay)
				{
					VideoPacingFieldTrialStr += bHasPacingFactor ? TEXT(",") : TEXT("");
					VideoPacingFieldTrialStr += FString::Printf(TEXT("max_delay:%.0f"), PacingMaxDelayMs);
				}
				VideoPacingFieldTrialStr += TEXT("/");
				FieldTrials += VideoPacingFieldTrialStr;
			}
		}

		return FieldTrials;
	}

	bool FPixelStreaming2RTCModule::InitializeEpicRtc()
	{
		EpicRtcVideoEncoderInitializers = { new FEpicRtcVideoEncoderInitializer() };
		EpicRtcVideoDecoderInitializers = { new FEpicRtcVideoDecoderInitializer() };

		EpicRtcPlatformConfig PlatformConfig{
			._memory = new FEpicRtcAllocator()
		};

		EpicRtcErrorCode Result = GetOrCreatePlatform(PlatformConfig, EpicRtcPlatform.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok && Result != EpicRtcErrorCode::FoundExistingPlatform)
		{
			UE_LOG(LogPixelStreaming2RTC, Warning, TEXT("Unable to create EpicRtc Platform. GetOrCreatePlatform returned %s"), *ToString(Result));
			return false;
		}

		FUtf8String EpicRtcFieldTrials(GetFieldTrials());

		WebsocketFactory = MakeRefCount<FEpicRtcWebsocketFactory>();

		StatsCollector = MakeRefCount<FEpicRtcStatsCollector>();

		// clang-format off
		EpicRtcConfig ConferenceConfig = {
			._websocketFactory = WebsocketFactory.GetReference(),
			._signallingType = EpicRtcSignallingType::PixelStreaming,
			._signingPlugin = nullptr,
			._migrationPlugin = nullptr,
			._audioDevicePlugin = nullptr,
			._audioConfig = {
				._tickAdm = true,
				._audioEncoderInitializers = {}, // Not needed because we use the inbuilt audio codecs
				._audioDecoderInitializers = {}, // Not needed because we use the inbuilt audio codecs
				._enableBuiltInAudioCodecs = true,
			},
			._videoConfig = {
				._videoEncoderInitializers = {
					._ptr = const_cast<const EpicRtcVideoEncoderInitializerInterface**>(EpicRtcVideoEncoderInitializers.GetData()),
					._size = (uint64_t)EpicRtcVideoEncoderInitializers.Num()
				},
				._videoDecoderInitializers = {
					._ptr = const_cast<const EpicRtcVideoDecoderInitializerInterface**>(EpicRtcVideoDecoderInitializers.GetData()),
					._size = (uint64_t)EpicRtcVideoDecoderInitializers.Num()
				},
				._enableBuiltInVideoCodecs = false
			},
			._fieldTrials = {
				._fieldTrials = ToEpicRtcStringView(EpicRtcFieldTrials),
				._isGlobal = 0
			},
			._logging = {
				._logger = new FEpicRtcLogsRedirector(MakeShared<FEpicRtcLogFilter>()),
#if !NO_LOGGING // When building WITH_SHIPPING by default .GetVerbosity() does not exist
				._level = UnrealLogToEpicRtcCategoryMap[LogPixelStreaming2EpicRtc.GetVerbosity()],
				._levelWebRtc = UnrealLogToEpicRtcCategoryMap[LogPixelStreaming2WebRtc.GetVerbosity()]
#endif
			},
			._stats = {
				._statsCollectorCallback = StatsCollector.GetReference(),
				._statsCollectorInterval = 1000,
				._jsonFormatOnly = false
			}
		};
		// clang-format on

		Result = EpicRtcPlatform->CreateConference(ToEpicRtcStringView(EpicRtcConferenceName), ConferenceConfig, EpicRtcConference.GetInitReference());
		if (Result != EpicRtcErrorCode::Ok)
		{
			UE_LOG(LogPixelStreaming2RTC, Warning, TEXT("Unable to create EpicRtc Conference: CreateConference returned %s"), *ToString(Result));
			return false;
		}

		return true;
	}

	/**
	 * End own methods
	 */
} // namespace UE::PixelStreaming2

IMPLEMENT_MODULE(UE::PixelStreaming2::FPixelStreaming2RTCModule, PixelStreaming2RTC)
