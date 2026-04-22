// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaStreamPlayer.h"

#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "MediaIOCoreAudioSampleBase.h"
#include "MediaIOCoreBinarySampleBase.h"
#include "MediaIOCoreEncodeTime.h"
#include "MediaIOCoreSamples.h"
#include "MediaIOCoreTextureSampleBase.h"
#include "MediaIOCoreTextureSampleConverter.h"
#include "NDIMediaLog.h"
#include "NDIMediaModule.h"
#include "NDIMediaSource.h"
#include "NDIMediaSourceOptions.h"
#include "NDIMediaTextureSample.h"
#include "NDIMediaTextureSampleConverter.h"
#include "NDIStreamReceiver.h"
#include "NDIStreamReceiverManager.h"

#define LOCTEXT_NAMESPACE "NDIMediaPlayer"

class FNDIMediaTextureSamplePool : public TMediaObjectPool<FNDIMediaTextureSample>
{};

/**
 * Implements a media audio sample for NDIMedia
 */
class FNDIMediaAudioSample : public FMediaIOCoreAudioSampleBase
{
	using Super = FMediaIOCoreAudioSampleBase;

public:
};

class FNDIMediaAudioSamplePool : public TMediaObjectPool<FNDIMediaAudioSample>
{};

/*
 * Implements a pool for NDI binary sample objects. 
 */
class FNDIMediaBinarySamplePool : public TMediaObjectPool<FMediaIOCoreBinarySampleBase> { };

FNDIMediaStreamPlayer::FNDIMediaStreamPlayer(IMediaEventSink& InEventSink)
	: Super(InEventSink)
	, NDIPlayerState(EMediaState::Closed)
	, EventSink(InEventSink)
	, TextureSamplePool(new FNDIMediaTextureSamplePool)
	, AudioSamplePool(new FNDIMediaAudioSamplePool)
	, MetadataSamplePool(new FNDIMediaBinarySamplePool)
{}

FNDIMediaStreamPlayer::~FNDIMediaStreamPlayer()
{
	Close();

	TextureSamplePool.Reset();
	AudioSamplePool.Reset();
}

FGuid FNDIMediaStreamPlayer::GetPlayerPluginGUID() const
{
	return FNDIMediaModule::PlayerPluginGUID;
}

#if WITH_EDITOR	
void FNDIMediaStreamPlayer::OnOptionsChanged(UObject* InOptions, FPropertyChangedEvent& InPropertyChanged)
{
	if (OptionsObject == InOptions)
	{
		if (UMediaSource* InMediaSource = Cast<UMediaSource>(InOptions))
		{
			// todo: some options can possibly be modified without needing a complete reset.
			// For now, handle any options changed by restarting the player.
			{
				TGuardValue ReopenGuard(bIsReopening, true);
				Close();
				Open(InMediaSource->GetUrl(), InMediaSource);
			}
		}
	}
}
#endif


bool FNDIMediaStreamPlayer::Open(const FString& InUrl, const IMediaOptions* InOptions)
{
	if (!Super::Open(InUrl, InOptions))
	{
		return false;
	}

#if WITH_EDITOR
	if (!bIsReopening)
	{
		OptionsObject = InOptions->ToUObject();
		UNDIMediaSource::OnOptionChanged.RemoveAll(this);
		UNDIMediaSource::OnOptionChanged.AddSP(this, &FNDIMediaStreamPlayer::OnOptionsChanged);
	}
#endif
	
	MaxNumVideoFrameBuffer = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::MaxVideoFrameBuffer, (int64)8);
	MaxNumAudioFrameBuffer = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::MaxAudioFrameBuffer, (int64)8);
	MaxNumMetadataFrameBuffer = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::MaxAncillaryFrameBuffer, (int64)8);
	bEncodeTimecodeInTexel = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::EncodeTimecodeInTexel, false);

	// Setup our different supported channels based on source settings
	SetupSampleChannels();

	// configure format information for base class
	AudioTrackFormat.BitsPerSample = 32;
	AudioTrackFormat.NumChannels = 0;
	AudioTrackFormat.SampleRate = 44100;
	AudioTrackFormat.TypeName = FString(TEXT("PCM"));

	// Video track format is only known when the first video frame is received.
	VideoTrackFormat.Dim = FIntPoint(0, 0);

	bCaptureVideo = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::CaptureVideo, true);
	bCaptureAudio = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::CaptureAudio, false);
	bCaptureAncillary = InOptions->GetMediaOption(UE::NDIMediaSourceOptions::CaptureAncillary, false);
	SupportedSampleTypes = bCaptureVideo ? EMediaIOSampleType::Video : EMediaIOSampleType::None;
	SupportedSampleTypes |= bCaptureAudio ? EMediaIOSampleType::Audio : EMediaIOSampleType::None;
	SupportedSampleTypes |= bCaptureAncillary ? EMediaIOSampleType::Metadata : EMediaIOSampleType::None;
	Samples->EnableTimedDataChannels(this, SupportedSampleTypes);

	FNDISourceSettings SourceSettings;
	SourceSettings.Bandwidth = static_cast<ENDIReceiverBandwidth>(InOptions->GetMediaOption(UE::NDIMediaSourceOptions::Bandwidth, static_cast<int64>(SourceSettings.Bandwidth)));
	SourceSettings.bCaptureAudio = bCaptureAudio;
	SourceSettings.bCaptureVideo = bCaptureVideo;

	FString Scheme;
	FString Location;
	if (InUrl.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
	{
		SourceSettings.SourceName = Location;
	}

	// Check if the receiver is already created by another object.
	if (FNDIMediaModule* Module = FNDIMediaModule::Get())
	{
		FNDIStreamReceiverManager& StreamReceiverManager = Module->GetStreamReceiverManager();
		Receiver = StreamReceiverManager.FindReceiver(SourceSettings.SourceName);

		if (!Receiver)
		{
			Receiver = MakeShared<FNDIStreamReceiver>(FNDIMediaModule::GetNDIRuntimeLibrary());
		}
	}
	
	if (!Receiver)
	{
		UE_LOG(LogNDIMedia, Error, TEXT("Failed to acquire NDI receiver."));
		return false;
	}
	
	// Hook into the video and audio captures
	VideoReceivedHandle = Receiver->OnVideoFrameReceived.AddRaw(this, &FNDIMediaStreamPlayer::HandleVideoFrameReceived);
	AudioReceivedHandle = Receiver->OnAudioFrameReceived.AddRaw(this, &FNDIMediaStreamPlayer::HandleAudioFrameReceived);

	// Control the player's state based on the receiver connecting and disconnecting
	ConnectedHandle = Receiver->OnConnected.AddLambda([this](FNDIStreamReceiver* receiver)
	{
		NDIPlayerState = EMediaState::Playing;
	});
	DisconnectedHandle = Receiver->OnDisconnected.AddLambda([this](FNDIStreamReceiver* receiver)
	{
		NDIPlayerState = EMediaState::Closed;
	});

	// Get ready to connect
	CurrentState = EMediaState::Preparing;
	NDIPlayerState = EMediaState::Preparing;
	EventSink.ReceiveMediaEvent(EMediaEvent::MediaConnecting);
	
	Receiver->SetSyncTimecodeToSource(InOptions->GetMediaOption(UE::NDIMediaSourceOptions::SyncTimecodeToSource, true));

	// Start up the receiver under the player's control.
	return Receiver->Initialize(SourceSettings, FNDIStreamReceiver::ECaptureMode::Manual);
}

void FNDIMediaStreamPlayer::Close()
{
	NDIPlayerState = EMediaState::Closed;

	if (Receiver != nullptr)
	{
		// Disconnect from receiver events
		Receiver->OnVideoFrameReceived.Remove(VideoReceivedHandle);
		VideoReceivedHandle.Reset();
		Receiver->OnAudioFrameReceived.Remove(AudioReceivedHandle);
		AudioReceivedHandle.Reset();
		Receiver->OnConnected.Remove(ConnectedHandle);
		ConnectedHandle.Reset();
		Receiver->OnDisconnected.Remove(DisconnectedHandle);
		DisconnectedHandle.Reset();

		Receiver.Reset();
	}

	TextureSamplePool->Reset();
	AudioSamplePool->Reset();
	MetadataSamplePool->Reset();

#if WITH_EDITOR
	if (!bIsReopening)
	{
		OptionsObject = nullptr;
		UNDIMediaSource::OnOptionChanged.RemoveAll(this);
	}
#endif

	Super::Close();
}

FString FNDIMediaStreamPlayer::GetStats() const
{
	FString Stats;

	if (Receiver)
	{
		const FNDIMediaReceiverPerformanceData PerformanceData = Receiver->GetPerformanceData();
		Stats += FString::Printf(TEXT("Video Frames: %lld"), PerformanceData.VideoFrames);
		Stats += FString::Printf(TEXT("Dropped Video Frames: %lld"), PerformanceData.DroppedVideoFrames);
		Stats += FString::Printf(TEXT("Audio Frames: %lld"), PerformanceData.AudioFrames);
		Stats += FString::Printf(TEXT("Dropped Audio Frames: %lld"), PerformanceData.DroppedAudioFrames);
		Stats += FString::Printf(TEXT("Metadata Frames: %lld"), PerformanceData.MetadataFrames);
		Stats += FString::Printf(TEXT("Dropped Metadata Frames: %lld"), PerformanceData.DroppedMetadataFrames);
	}
	else
	{
		Stats = FString(TEXT("Receiver not available."));
	}
	return Stats;
}

void FNDIMediaStreamPlayer::TickInput(FTimespan InDeltaTime, FTimespan InTime)
{
	// Update player state
	EMediaState NewState = NDIPlayerState;

	if (NewState != CurrentState)
	{
		CurrentState = NewState;
		if (CurrentState == EMediaState::Playing)
		{
			EventSink.ReceiveMediaEvent(EMediaEvent::TracksChanged);
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpened);
			EventSink.ReceiveMediaEvent(EMediaEvent::PlaybackResumed);
		}
		else if (NewState == EMediaState::Error)
		{
			EventSink.ReceiveMediaEvent(EMediaEvent::MediaOpenFailed);
			Close();
		}
	}

	if (CurrentState != EMediaState::Playing)
	{
		return;
	}

	TickTimeManagement();
}

bool FNDIMediaStreamPlayer::HasVideoSamplesWithCustomConverter() const
{
	const TArray<TSharedPtr<IMediaTextureSample>> TextureSamples = Samples->GetVideoSamples();
	// Check the latest received sample only. New samples are inserted at 0.
	if (!TextureSamples.IsEmpty() && TextureSamples[0])
	{
		// We have to explicitly check the CustomConverter of the NDI sample. 
		// We can't use GetMediaTextureSampleConverter() because it may return the JITR converter.
		return static_cast<const FNDIMediaTextureSample*>(TextureSamples[0].Get())->CustomConverter != nullptr;
	}
	return false;
}

void FNDIMediaStreamPlayer::TickFetch(FTimespan InDeltaTime, FTimespan InTime)
{
	Super::TickFetch(InDeltaTime, InTime);

	if (CurrentState == EMediaState::Preparing || CurrentState == EMediaState::Playing)
	{
		if (Receiver != nullptr)
		{
			if (bCaptureAudio)
			{
				Receiver->FetchAudio(InTime);
			}
			if (bCaptureVideo)
			{
				Receiver->FetchVideo(InTime);
			}
			if (bCaptureAncillary)
			{
				// Potential improvement: limit how much metadata is processed, to avoid appearing to lock up due to a metadata flood
				while (Receiver->FetchMetadata(InTime)) {}
			}
		}
	}

	if (CurrentState == EMediaState::Playing)
	{
		// No need to lock here. That info is only used for debug information.
		AudioTrackFormat.NumChannels = NDIThreadAudioChannels;
		AudioTrackFormat.SampleRate = NDIThreadAudioSampleRate;

		if (Receiver)
		{
			VideoFrameRate = Receiver->GetCurrentFrameRate();
			VideoTrackFormat.FrameRates = TRange<float>(VideoFrameRate.AsDecimal());
			VideoTrackFormat.FrameRate = VideoFrameRate.AsDecimal();
			//VideoTrackFormat.TypeName = Configuration.Configuration.MediaMode.GetModeName().ToString();

			// Detect changes in video format.
			if (VideoTrackFormat.Dim != Receiver->GetCurrentResolution())
			{
				VideoTrackFormat.Dim = Receiver->GetCurrentResolution();
				NotifyVideoFormatDetected();
			}

			// Detect changes in frame rate settings for timed data channels.
			if (BaseSettings.FrameRate != VideoFrameRate)
			{
				BaseSettings.FrameRate = VideoFrameRate;
				SetupSampleChannels();
			}
		}

		VerifyFrameDropCount();
	}
}

void FNDIMediaStreamPlayer::HandleVideoFrameReceived(FNDIStreamReceiver* InReceiver, const NDIlib_video_frame_v2_t& InVideoFrame, const FTimespan& InTime)
{
	if (!InReceiver)
	{
		return;
	}

	TSharedRef<FNDIMediaTextureSample> TextureSample = TextureSamplePool->AcquireShared();
	
	const UE::MediaIOCore::FColorFormatArgs ColorFormatArgs(
		bOverrideSourceEncoding ?  static_cast<UE::Color::EEncoding>(OverrideSourceEncoding) : UE::Color::EEncoding::sRGB,
		bOverrideSourceColorSpace ? static_cast<UE::Color::EColorSpace>(OverrideSourceColorSpace) : UE::Color::EColorSpace::sRGB);

	FTimecode SourceTimecode = InReceiver->GetCurrentTimecode();

	FTimespan SampleTime = InTime;

	if (EvaluationType == EMediaIOSampleEvaluationType::Timecode)
	{
		SampleTime = SourceTimecode.ToTimespan(InReceiver->GetCurrentFrameRate());
	}

	if (TextureSample->Initialize(InVideoFrame, ColorFormatArgs, SampleTime, SourceTimecode))
	{
		if (TextureSample->CustomConverter)
		{
			TextureSample->CustomConverter->Setup(TextureSample);
		}

		TextureSample->SetColorConversionSettings(OCIOSettings);

		if (bEncodeTimecodeInTexel && InVideoFrame.frame_format_type == NDIlib_frame_format_type_progressive)
		{
			EMediaIOCoreEncodePixelFormat EncodePixelFormat;
			bool bEncodeSupported = true;

			if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_UYVY
				|| InVideoFrame.FourCC == NDIlib_FourCC_video_type_UYVA)
			{
				// Note: for UYVA, we can write in the UYVY part (even if it ends up being transparent).
				// todo: add support in FMediaIOCoreEncodeTime for single channel (R) format.
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharUYVY;
			}
			else if (InVideoFrame.FourCC == NDIlib_FourCC_video_type_BGRA
				|| InVideoFrame.FourCC == NDIlib_FourCC_video_type_RGBA
				|| InVideoFrame.FourCC == NDIlib_FourCC_video_type_BGRX)
			{
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharBGRA;
			}
			else
			{
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharUYVY;
				bEncodeSupported = false;
			}
			
			if (bEncodeSupported)
			{
				FMediaIOCoreEncodeTime EncodeTime(EncodePixelFormat, const_cast<void*>(TextureSample->GetBuffer()), InVideoFrame.line_stride_in_bytes, InVideoFrame.xres, InVideoFrame.yres);
				EncodeTime.Render(SourceTimecode.Hours, SourceTimecode.Minutes, SourceTimecode.Seconds, SourceTimecode.Frames);
			}
		}
		
		AddVideoSample(TextureSample);
	}
}

void FNDIMediaStreamPlayer::HandleAudioFrameReceived(FNDIStreamReceiver* InReceiver, const NDIlib_audio_frame_v2_t& InAudioFrame, const FTimespan& InTime)
{
	FNDIMediaRuntimeLibrary* NdiLib = Receiver->GetNdiLib();
	if (!NdiLib || !NdiLib->IsLoaded())
	{
		return;
	}
	
	TSharedRef<FNDIMediaAudioSample> AudioSample = AudioSamplePool->AcquireShared();

	// UE wants 32bit signed interleaved audio data, so need to convert the NDI audio.
	// Fortunately the NDI library has a utility function to do that.

	// Get a buffer to convert to
	const int32 available_samples = InAudioFrame.no_samples * InAudioFrame.no_channels;
	void* SampleBuffer = AudioSample->RequestBuffer(available_samples);

	if (SampleBuffer != nullptr)
	{
		// Format to convert to
		NDIlib_audio_frame_interleaved_32s_t audio_frame_32s(
			InAudioFrame.sample_rate,
			InAudioFrame.no_channels,
			InAudioFrame.no_samples,
			InAudioFrame.timecode,
			20,
			static_cast<int32_t*>(SampleBuffer));

		// Convert received NDI audio
		NdiLib->Lib->util_audio_to_interleaved_32s_v2(&InAudioFrame, &audio_frame_32s);
		
		// Supply converted audio data
		if (AudioSample->SetProperties(available_samples
			, audio_frame_32s.no_channels
			, audio_frame_32s.sample_rate
			, InTime
			, TOptional<FTimecode>()))
		{
			NDIThreadAudioChannels = audio_frame_32s.no_channels;
			NDIThreadAudioSampleRate = audio_frame_32s.sample_rate;
			
			AddAudioSample(AudioSample);
		}
	}
}

void FNDIMediaStreamPlayer::VerifyFrameDropCount()
{
	// todo
}

bool FNDIMediaStreamPlayer::IsHardwareReady() const
{
	return NDIPlayerState == EMediaState::Playing;
}

void FNDIMediaStreamPlayer::SetupSampleChannels()
{
	FMediaIOSamplingSettings VideoSettings = BaseSettings;
	VideoSettings.BufferSize = MaxNumVideoFrameBuffer;
	Samples->InitializeVideoBuffer(VideoSettings);

	FMediaIOSamplingSettings AudioSettings = BaseSettings;
	AudioSettings.BufferSize = MaxNumAudioFrameBuffer;
	Samples->InitializeAudioBuffer(AudioSettings);

	FMediaIOSamplingSettings MetadataSettings = BaseSettings;
	MetadataSettings.BufferSize = MaxNumMetadataFrameBuffer;
	Samples->InitializeMetadataBuffer(MetadataSettings);
}

uint32 FNDIMediaStreamPlayer::GetNumVideoFrameBuffers() const
{
	return MaxNumVideoFrameBuffer;
}

TSharedPtr<FMediaIOCoreTextureSampleBase> FNDIMediaStreamPlayer::AcquireTextureSample_AnyThread() const
{
	// Needed by the deinterlacer and JITR.
	return TextureSamplePool->AcquireShared();
}

/**
 * Implementation of a sample converter that is used in the JITR code path.
 * It behaves like the base class if the samples don't have a custom converter.
 * When NDI samples have a custom converter, this wrapping converter will
 * call it on the Proxy sample to render in the provided destination render target.
 * In that case, the converter is not a "preprocess only" but rather just a "default" converter.
 */
class FNDIMediaTextureSampleConverterWrapper : public FMediaIOCoreTextureSampleConverter
{
public:
	//~ Begin IMediaTextureSampleConverter
	virtual bool Convert(FRHICommandListImmediate& RHICmdList, FTextureRHIRef& InDstTexture, const FConversionHints& Hints) override
	{
		// Base class will set up the proxy sample with the selected sample according to JITR logic.
		if (!FMediaIOCoreTextureSampleConverter::Convert(RHICmdList, InDstTexture, Hints))
		{
			return false;
		}

		TSharedPtr<FMediaIOCoreTextureSampleBase> ProxySample = JITRProxySample.Pin();
		if (!ProxySample.IsValid())
		{
			return false;
		}

		// We should then be able to actually convert the sample to the destination using the NDI custom converter from the sample.
		const FNDIMediaTextureSample* ProxySampleNdi = static_cast<FNDIMediaTextureSample*>(ProxySample.Get());

		if (ProxySampleNdi->bIsCustomFormat)
		{
			if (IMediaTextureSampleConverter* CustomConverter = ProxySampleNdi->CustomConverter.Get())
			{
				// This can be null if the stream switches format on the fly and we have stale samples with a custom format (ignore them).
				if (InDstTexture.IsValid())
				{
					return CustomConverter->Convert(RHICmdList, InDstTexture, Hints);
				}
			}
			return false;
		}

		// if it is not a custom format, continue the conversion through media texture pipeline instead.
		return true;
	}

	virtual uint32 GetConverterInfoFlags() const override
	{
		// We need to check in the player if the samples will have a custom converter, if so
		// we need to indicate that the converter will do a full conversion by returning the default flags.
		if (TSharedPtr<FMediaIOCoreTextureSampleBase> ProxySample = JITRProxySample.Pin())
		{
			if (TSharedPtr<FMediaIOCorePlayerBase> Player = ProxySample->GetPlayer())
			{
				if (static_cast<FNDIMediaStreamPlayer*>(Player.Get())->HasVideoSamplesWithCustomConverter())
				{
					// Behave like a default converter (that will actually do the conversion).
					return ConverterInfoFlags_Default;
				}
			}
		}

		return FMediaIOCoreTextureSampleConverter::GetConverterInfoFlags();
	}
	//~ End IMediaTextureSampleConverter
};

TSharedPtr<FMediaIOCoreTextureSampleConverter> FNDIMediaStreamPlayer::CreateTextureSampleConverter() const
{
	return MakeShared<FNDIMediaTextureSampleConverterWrapper>();
}

//~ ITimedDataInput interface
#if WITH_EDITOR
const FSlateBrush* FNDIMediaStreamPlayer::GetDisplayIcon() const
{
	return nullptr;
}
#endif


#undef LOCTEXT_NAMESPACE
