// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLocalLiveLinkSourceBlueprint.h"
#include "MetaHumanVideoLiveLinkSource.h"
#include "MetaHumanVideoLiveLinkSourceSettings.h"
#include "MetaHumanVideoLiveLinkSubjectSettings.h"
#include "MetaHumanAudioLiveLinkSource.h"
#include "MetaHumanAudioLiveLinkSourceSettings.h"
#include "MetaHumanAudioLiveLinkSubjectSettings.h"
#include "MetaHumanPipelineMediaPlayerNode.h"

#include "MediaCaptureSupport.h"
#include "MediaBundle.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"

#include "UObject/Package.h"
#include "HAL/RunnableThread.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Features/IModularFeatures.h"
#include "UObject/GCObject.h"



// A class for querying track and format info of media. Given a device it will fill in a track list.
// Additionally, if a specific track is specified it will fill in a format list for that track.

class FMediaPlayerQuery : public FRunnable, public FGCObject, public TSharedFromThis<FMediaPlayerQuery>
{
public:

	FMediaPlayerQuery(const FMetaHumanLiveLinkVideoDevice& InVideoDevice, const FMetaHumanLiveLinkVideoTrack& InVideoTrack)
	{
		VideoDevice = InVideoDevice;
		VideoTrack = InVideoTrack;
		bIsVideo = true;
	}

	FMediaPlayerQuery(const FMetaHumanLiveLinkAudioDevice& InAudioDevice, const FMetaHumanLiveLinkAudioTrack& InAudioTrack)
	{
		AudioDevice = InAudioDevice;
		AudioTrack = InAudioTrack;
		bIsVideo = false;
	}

	~FMediaPlayerQuery()
	{
		if (MediaPlayer)
		{
			MediaPlayer->Close();
		}
	}

	void Start(bool bInFormatsFiltered, float InTimeout)
	{
		bFormatsFiltered = bInFormatsFiltered;
		Timeout = InTimeout;

		bTimedOut = false;
		bOpened = false;

		const FString Url = bIsVideo ? VideoDevice.Url : AudioDevice.Url;

		if (Url.IsEmpty())
		{
			return;
		}
		else if (Url.StartsWith(UE::MetaHuman::Pipeline::FMediaPlayerNode::BundleURL))
		{
			// Add dummy track and format for media bundle cases. This makes the logic of any
			// combo boxes these are typically wired up to much easier since there is then
			// a direct parallel between regular devices (eg webcam) and media bundles.
			// It then only requires minimal and simple special case handling to hide the track
			// and format combo boxes for the media bundle case so that these dummy values are never shown.

			if (bIsVideo)
			{
				FMetaHumanLiveLinkVideoTrack Track;
				Track.VideoDevice = VideoDevice;
				Track.Name = "Unknown";
				VideoTracks.Add(Track);

				FMetaHumanLiveLinkVideoFormat Format;
				Format.VideoTrack = Track;
				Format.Name = "Unknown";
				VideoFormats.Add(Format);
			}
			else
			{
				FMetaHumanLiveLinkAudioTrack Track;
				Track.AudioDevice = AudioDevice;
				Track.Name = "Unknown";
				AudioTracks.Add(Track);

				FMetaHumanLiveLinkAudioFormat Format;
				Format.AudioTrack = Track;
				Format.Name = "Unknown";
				AudioFormats.Add(Format);
			}
		}
		else
		{
			MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage());
			check(MediaPlayer);

			MediaPlayer->OnMediaEvent().AddSP(this, &FMediaPlayerQuery::OnMediaPlayerEvent);
			MediaPlayer->PlayOnOpen = false;

			MediaPlayer->GetPlayerFacade()->SetAreEventsSafeForAnyThread(true);

			MediaPlayer->OpenUrl(Url); // async call, picked up in OnMediaPlayerEvent function below

			TUniquePtr<FRunnableThread> Thread;

			Thread.Reset(FRunnableThread::Create(this, TEXT("FMediaPlayerQuery"), 0, TPri_BelowNormal));

			Thread->WaitForCompletion();
		}
	}

	virtual uint32 Run() override
	{
		const double Start = FPlatformTime::Seconds();

		while (FPlatformTime::Seconds() < Start + Timeout)
		{
			MediaPlayer->GetPlayerFacade()->TickInput(FTimespan(100), FTimespan(100)); // arbitrary tick interval

			if (bOpened)
			{
				break;
			}

			FPlatformProcess::Sleep(0.01f);
		}

		bTimedOut = !bOpened;

		return 0;
	}

	void OnMediaPlayerEvent(EMediaEvent InEvent)
	{
		// Dont rely on InEvent == EMediaEvent::MediaOpened in this function, EMediaEvent::MediaOpenFailed
		// may also suffice for our needs here to just list tracks/format. 
		// We get the failed case for the BRIO camera which has a strange video track 0 (a MSN audio track).
		// Without the codec for that we can get the "failed" case even though that track wont be used in practice. 
		// Video track 1 contains all the useable formats for the BRIO.
		// One solution would be to install the codec, but that would be a step required of all end-users
		// and is a codec thats never needed in practice. Better to treat the "MediaOpenFailed" more
		// as a warning and carry on. Error handling when you actually select a video track/format and
		// attempt to play it will catch any real errors.

		if (InEvent == EMediaEvent::MediaOpened || InEvent == EMediaEvent::MediaOpenFailed)
		{
			const int32 NumTracks = MediaPlayer->GetNumTracks(bIsVideo ? EMediaPlayerTrack::Video : EMediaPlayerTrack::Audio);

			for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
			{
				if (bIsVideo)
				{
					FMetaHumanLiveLinkVideoTrack Track;

					Track.Index = TrackIndex;
					Track.Name = FString::FromInt(TrackIndex);
					Track.VideoDevice = VideoDevice;

					VideoTracks.Add(Track);
				}
				else
				{
					FMetaHumanLiveLinkAudioTrack Track;

					Track.Index = TrackIndex;
					Track.Name = FString::FromInt(TrackIndex);
					Track.AudioDevice = AudioDevice;

					AudioTracks.Add(Track);
				}
			}

			if (bIsVideo && !VideoTrack.Name.IsEmpty()) // Need to fill in video track format list?
			{
				const int32 Track = VideoTrack.Index;
				const int32 NumTrackFormats = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, Track);

				for (int32 TrackFormat = 0; TrackFormat < NumTrackFormats; ++TrackFormat)
				{
					FMetaHumanLiveLinkVideoFormat Format;

					Format.Index = TrackFormat;
					Format.Resolution = MediaPlayer->GetVideoTrackDimensions(Track, TrackFormat);
					Format.FrameRate = MediaPlayer->GetVideoTrackFrameRate(Track, TrackFormat);
					Format.Type = MediaPlayer->GetVideoTrackType(Track, TrackFormat);

					if (!bFormatsFiltered ||
						((Format.Type == TEXT("NV12") || Format.Type == TEXT("YUY2") || Format.Type == TEXT("UYVY") || Format.Type == TEXT("BGRA")) && Format.Resolution.X > 500 && Format.Resolution.Y > 500 && Format.FrameRate >= 24))
					{
						Format.Name = FString::Printf(TEXT("%d: %s %ix%i"), TrackFormat, *Format.Type, Format.Resolution.X, Format.Resolution.Y);

						const int32 IntFrameRate = FMath::RoundToInt(Format.FrameRate);
						if (FMath::Abs(Format.FrameRate - IntFrameRate) > 0.0001)
						{
							Format.Name += FString::Printf(TEXT(" %.2f fps"), Format.FrameRate);
						}
						else
						{
							Format.Name += FString::Printf(TEXT(" %i fps"), IntFrameRate);
						}

						Format.VideoTrack = VideoTrack;

						VideoFormats.Add(Format);
					}
				}

				VideoFormats.Sort([](const FMetaHumanLiveLinkVideoFormat& InItem1, const FMetaHumanLiveLinkVideoFormat& InItem2)
				{
					// Sort first by fps, then res, then type
					if (InItem1.FrameRate == InItem2.FrameRate)
					{
						if (InItem1.Resolution == InItem2.Resolution)
						{
							return InItem1.Type > InItem2.Type;
						}
						else
						{
							return InItem1.Resolution.Size() > InItem2.Resolution.Size();
						}
					}
					else
					{
						return InItem1.FrameRate > InItem2.FrameRate;
					}
				});
			}
			else if (!bIsVideo && !AudioTrack.Name.IsEmpty()) // Need to fill in audio track format list?
			{
				const int32 Track = AudioTrack.Index;
				const int32 NumTrackFormats = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Audio, Track);

				for (int32 TrackFormat = 0; TrackFormat < NumTrackFormats; ++TrackFormat)
				{
					FMetaHumanLiveLinkAudioFormat Format;

					Format.Index = TrackFormat;
					Format.NumChannels = MediaPlayer->GetAudioTrackChannels(Track, TrackFormat);
					Format.SampleRate = MediaPlayer->GetAudioTrackSampleRate(Track, TrackFormat);
					Format.Type = MediaPlayer->GetAudioTrackType(Track, TrackFormat);
					Format.Name = FString::Printf(TEXT("%d: %s %i channels @ %i Hz"), TrackFormat, *Format.Type, Format.NumChannels, Format.SampleRate);
					Format.AudioTrack = AudioTrack;

					AudioFormats.Add(Format);
				}
			}

			bOpened = true;
		}
	}

	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override
	{
		InCollector.AddReferencedObject(MediaPlayer);
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("FMediaPlayerQuery");
	}

	TObjectPtr<UMediaPlayer> MediaPlayer;

	FMetaHumanLiveLinkVideoDevice VideoDevice;
	FMetaHumanLiveLinkVideoTrack VideoTrack;

	FMetaHumanLiveLinkAudioDevice AudioDevice;
	FMetaHumanLiveLinkAudioTrack AudioTrack;

	bool bIsVideo = true;

	bool bFormatsFiltered = true;
	float Timeout = 5;

	bool bTimedOut = false;
	bool bOpened = false;

	TArray<FMetaHumanLiveLinkVideoTrack> VideoTracks;
	TArray<FMetaHumanLiveLinkVideoFormat> VideoFormats;

	TArray<FMetaHumanLiveLinkAudioTrack> AudioTracks;
	TArray<FMetaHumanLiveLinkAudioFormat> AudioFormats;
};

static void AddDevice(TArray<FMediaCaptureDeviceInfo> InDeviceInfo, bool bInIncludeMediaBundles, TFunction<void (const FString&, const FString&, bool)> InAddFunction)
{
	for (const FMediaCaptureDeviceInfo& DeviceInfo : InDeviceInfo)
	{
		InAddFunction(DeviceInfo.DisplayName.ToString(), DeviceInfo.Url, false /* IsMediaBundle */);
	}

	if (bInIncludeMediaBundles)
	{
		const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		TArray<FAssetData> MediaBundles;
		AssetRegistry.GetAssetsByClass(UMediaBundle::StaticClass()->GetClassPathName(), MediaBundles);

		for (const FAssetData& MediaBundle : MediaBundles)
		{
			UObject* Bundle = MediaBundle.GetAsset();
			if (Bundle)
			{
				InAddFunction(Bundle->GetName(), UE::MetaHuman::Pipeline::FMediaPlayerNode::BundleURL + Bundle->GetPathName(), true /* IsMediaBundle */);
			}
		}
	}
}



void UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoDevices(TArray<FMetaHumanLiveLinkVideoDevice>& OutVideoDevices, bool bInIncludeMediaBundles)
{
	TArray<FMediaCaptureDeviceInfo> VideoDeviceInfos;
	MediaCaptureSupport::EnumerateVideoCaptureDevices(VideoDeviceInfos);

	AddDevice(VideoDeviceInfos, bInIncludeMediaBundles, [&OutVideoDevices](const FString& InName, const FString& InUrl, bool bInIsMediaBundle)
	{
		FMetaHumanLiveLinkVideoDevice VideoDevice;

		VideoDevice.Name = InName;
		VideoDevice.Url = InUrl;
		VideoDevice.IsMediaBundle = bInIsMediaBundle;

		OutVideoDevices.Add(VideoDevice);
	});
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoTracks(const FMetaHumanLiveLinkVideoDevice& InVideoDevice, TArray<FMetaHumanLiveLinkVideoTrack>& OutVideoTracks, bool& bOutTimedOut, float InTimeout)
{
	TSharedPtr<FMediaPlayerQuery> MediaPlayerQuery = MakeShared<FMediaPlayerQuery>(InVideoDevice, FMetaHumanLiveLinkVideoTrack());
	MediaPlayerQuery->Start(true /* bInFormatsFiltered, N/A here */, InTimeout);

	OutVideoTracks = MediaPlayerQuery->VideoTracks;
	bOutTimedOut = MediaPlayerQuery->bTimedOut;
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoFormats(const FMetaHumanLiveLinkVideoTrack& InVideoTrack, TArray<FMetaHumanLiveLinkVideoFormat>& OutVideoFormats, bool& bOutTimedOut, bool bInFilterFormats, float InTimeout)
{
	TSharedPtr<FMediaPlayerQuery> MediaPlayerQuery = MakeShared<FMediaPlayerQuery>(InVideoTrack.VideoDevice, InVideoTrack);
	MediaPlayerQuery->Start(bInFilterFormats, InTimeout);

	OutVideoFormats = MediaPlayerQuery->VideoFormats;
	bOutTimedOut = MediaPlayerQuery->bTimedOut;
}

void UMetaHumanLocalLiveLinkSourceBlueprint::CreateVideoSource(FLiveLinkSourceHandle& OutVideoSource, bool& bOutSucceeded)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	TSharedPtr<FMetaHumanLocalLiveLinkSource> VideoSource = MakeShared<FMetaHumanVideoLiveLinkSource>();

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	const FGuid GUID = LiveLinkClient.AddSource(VideoSource);

	OutVideoSource.SetSourcePointer(VideoSource);

	bOutSucceeded = GUID.IsValid();
}

void UMetaHumanLocalLiveLinkSourceBlueprint::CreateVideoSubject(const FLiveLinkSourceHandle& InVideoSource, const FMetaHumanLiveLinkVideoFormat& InVideoFormat, const FString& InSubjectName, FLiveLinkSubjectKey& OutVideoSubject, bool& bOutSucceeded, float InStartTimeout, float InFormatWaitTime, float InSampleTimeout)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	if (InVideoSource.SourcePointer && InVideoSource.SourcePointer->GetSourceType().ToString() == FMetaHumanVideoLiveLinkSource::SourceType.ToString())
	{
		FMetaHumanVideoLiveLinkSource* VideoSource = (FMetaHumanVideoLiveLinkSource*) InVideoSource.SourcePointer.Get();

		UMetaHumanVideoLiveLinkSourceSettings* VideoSourceSettings = Cast<UMetaHumanVideoLiveLinkSourceSettings>(LiveLinkClient.GetSourceSettings(VideoSource->GetSourceGuid()));

		if (VideoSourceSettings)
		{
			UMetaHumanVideoLiveLinkSubjectSettings* VideoSubjectSettings = NewObject<UMetaHumanVideoLiveLinkSubjectSettings>(GetTransientPackage());

			VideoSubjectSettings->MediaSourceCreateParams.VideoName = InVideoFormat.VideoTrack.VideoDevice.Name;
			VideoSubjectSettings->MediaSourceCreateParams.VideoURL = InVideoFormat.VideoTrack.VideoDevice.Url;
			VideoSubjectSettings->MediaSourceCreateParams.VideoTrack = InVideoFormat.VideoTrack.Index;
			VideoSubjectSettings->MediaSourceCreateParams.VideoTrackFormat = InVideoFormat.Index;
			VideoSubjectSettings->MediaSourceCreateParams.VideoTrackFormatName = InVideoFormat.Name;
			VideoSubjectSettings->MediaSourceCreateParams.StartTimeout = InStartTimeout;
			VideoSubjectSettings->MediaSourceCreateParams.FormatWaitTime = InFormatWaitTime;
			VideoSubjectSettings->MediaSourceCreateParams.SampleTimeout = InSampleTimeout;

			VideoSubjectSettings->Setup();

			OutVideoSubject = VideoSourceSettings->RequestSubjectCreation(InSubjectName, VideoSubjectSettings);
		}
	}

	bOutSucceeded = OutVideoSubject.Source.IsValid();
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioDevices(TArray<FMetaHumanLiveLinkAudioDevice>& OutAudioDevices, bool bInIncludeMediaBundles)
{
	TArray<FMediaCaptureDeviceInfo> AudioDeviceInfos;
	MediaCaptureSupport::EnumerateAudioCaptureDevices(AudioDeviceInfos);

	AddDevice(AudioDeviceInfos, bInIncludeMediaBundles, [&OutAudioDevices](const FString& InName, const FString& InUrl, bool bInIsMediaBundle)
	{
		FMetaHumanLiveLinkAudioDevice AudioDevice;

		AudioDevice.Name = InName;
		AudioDevice.Url = InUrl;
		AudioDevice.IsMediaBundle = bInIsMediaBundle;

		OutAudioDevices.Add(AudioDevice);
	});
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioTracks(const FMetaHumanLiveLinkAudioDevice& InAudioDevice, TArray<FMetaHumanLiveLinkAudioTrack>& OutAudioTracks, bool& bOutTimedOut, float InTimeout)
{
	TSharedPtr<FMediaPlayerQuery> MediaPlayerQuery = MakeShared<FMediaPlayerQuery>(InAudioDevice, FMetaHumanLiveLinkAudioTrack());
	MediaPlayerQuery->Start(true /* bInFormatsFiltered, N/A here */, InTimeout);

	OutAudioTracks = MediaPlayerQuery->AudioTracks;
	bOutTimedOut = MediaPlayerQuery->bTimedOut;
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioFormats(const FMetaHumanLiveLinkAudioTrack& InAudioTrack, TArray<FMetaHumanLiveLinkAudioFormat>& OutAudioFormats, bool& bOutTimedOut, float InTimeout)
{
	TSharedPtr<FMediaPlayerQuery> MediaPlayerQuery = MakeShared<FMediaPlayerQuery>(InAudioTrack.AudioDevice, InAudioTrack);
	MediaPlayerQuery->Start(true /* bInFormatsFiltered, N/A here */, InTimeout);

	OutAudioFormats = MediaPlayerQuery->AudioFormats;
	bOutTimedOut = MediaPlayerQuery->bTimedOut;
}

void UMetaHumanLocalLiveLinkSourceBlueprint::CreateAudioSource(FLiveLinkSourceHandle& OutAudioSource, bool& bOutSucceeded)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	TSharedPtr<FMetaHumanLocalLiveLinkSource> AudioSource = MakeShared<FMetaHumanAudioLiveLinkSource>();

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	const FGuid GUID = LiveLinkClient.AddSource(AudioSource);

	OutAudioSource.SetSourcePointer(AudioSource);

	bOutSucceeded = GUID.IsValid();
}

void UMetaHumanLocalLiveLinkSourceBlueprint::CreateAudioSubject(const FLiveLinkSourceHandle& InAudioSource, const FMetaHumanLiveLinkAudioFormat& InAudioFormat, const FString& InSubjectName, FLiveLinkSubjectKey& OutAudioSubject, bool& bOutSucceeded, float InStartTimeout, float InFormatWaitTime, float InSampleTimeout)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		bOutSucceeded = false;
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	if (InAudioSource.SourcePointer && InAudioSource.SourcePointer->GetSourceType().ToString() == FMetaHumanAudioLiveLinkSource::SourceType.ToString())
	{
		FMetaHumanAudioLiveLinkSource* AudioSource = (FMetaHumanAudioLiveLinkSource*) InAudioSource.SourcePointer.Get();

		UMetaHumanAudioLiveLinkSourceSettings* AudioSourceSettings = Cast<UMetaHumanAudioLiveLinkSourceSettings>(LiveLinkClient.GetSourceSettings(AudioSource->GetSourceGuid()));

		if (AudioSourceSettings)
		{
			UMetaHumanAudioLiveLinkSubjectSettings* AudioSubjectSettings = NewObject<UMetaHumanAudioLiveLinkSubjectSettings>(GetTransientPackage());

			AudioSubjectSettings->MediaSourceCreateParams.AudioName = InAudioFormat.AudioTrack.AudioDevice.Name;
			AudioSubjectSettings->MediaSourceCreateParams.AudioURL = InAudioFormat.AudioTrack.AudioDevice.Url;
			AudioSubjectSettings->MediaSourceCreateParams.AudioTrack = InAudioFormat.AudioTrack.Index;
			AudioSubjectSettings->MediaSourceCreateParams.AudioTrackFormat = InAudioFormat.Index;
			AudioSubjectSettings->MediaSourceCreateParams.AudioTrackFormatName = InAudioFormat.Name;
			AudioSubjectSettings->MediaSourceCreateParams.StartTimeout = InStartTimeout;
			AudioSubjectSettings->MediaSourceCreateParams.FormatWaitTime = InFormatWaitTime;
			AudioSubjectSettings->MediaSourceCreateParams.SampleTimeout = InSampleTimeout;

			AudioSubjectSettings->Setup();

			OutAudioSubject = AudioSourceSettings->RequestSubjectCreation(InSubjectName, AudioSubjectSettings);
		}
	}

	bOutSucceeded = OutAudioSubject.Source.IsValid();
}

void UMetaHumanLocalLiveLinkSourceBlueprint::GetSubjectSettings(const FLiveLinkSubjectKey& InSubject, UObject*& OutSettings)
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		OutSettings = nullptr;
		return;
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	OutSettings = LiveLinkClient.GetSubjectSettings(InSubject);
}
