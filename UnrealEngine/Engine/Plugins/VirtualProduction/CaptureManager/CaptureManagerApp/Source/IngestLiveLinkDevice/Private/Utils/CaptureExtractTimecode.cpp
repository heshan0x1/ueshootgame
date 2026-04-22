// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/CaptureExtractTimecode.h"

#include "Audio.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "MediaPlaylist.h"
#include "Sound/SoundWaveTimecodeInfo.h"
#include "UObject/Package.h"
#include "Settings/CaptureManagerSettings.h"
#include "Logging/LogMacros.h"
#include "IElectraPlayerPluginModule.h"
#include "IMediaPlayer.h"
#include "IMediaOptions.h"
#include "IMediaEventSink.h"
#include "IMediaTracks.h"

#include "ParseTakeUtils.h"

#define LOCTEXT_NAMESPACE "CaptureExtractTimecode"

DEFINE_LOG_CATEGORY_STATIC(LogCaptureExtractTimecode, Log, All);

FCaptureExtractVideoInfo::FResult FCaptureExtractVideoInfo::Create(const FString& InFilePath)
{
	using namespace UE::CaptureManager;

	FCaptureExtractVideoInfo VideoInfoExtractor;

	FExtractResult Result = VideoInfoExtractor.ExtractInfo(InFilePath);
	if (Result.HasError())
	{
		return MakeError(Result.StealError());
	}

	return MakeValue(MoveTemp(VideoInfoExtractor));
}

FFrameRate FCaptureExtractVideoInfo::GetFrameRate() const
{
	return FrameRate;
}

FTimecode FCaptureExtractVideoInfo::GetTimecode() const
{
	return TimecodeInfo.Timecode;
}

FFrameRate FCaptureExtractVideoInfo::GetTimecodeRate() const
{
	return TimecodeInfo.TimecodeRate;
}

FCaptureExtractVideoInfo::FCaptureExtractVideoInfo()
	: FrameRate(1, 1)
{
}

FCaptureExtractVideoInfo::FExtractResult FCaptureExtractVideoInfo::ExtractInfo(const FString& InFilePath)
{
	using namespace UE::CaptureManager;

	// Replace any backslashes to avoid escape code issues while logging the file path
	const FString FilePath = InFilePath.Replace(TEXT("\\"), TEXT("/"));
	if (FilePath.IsEmpty())
	{
		return MakeError(ECaptureExtractInfoError::InternalError);
	}

	FCaptureExtractVideoInfo::FExtractResult Result = ExtractInfoUsingElectraPlayer(InFilePath);

	if (Result.HasError())
	{
		const UCaptureManagerSettings* Settings = GetDefault<UCaptureManagerSettings>();

		if (Settings->bEnableThirdPartyEncoder)
		{
			FString EncoderPath = Settings->ThirdPartyEncoder.FilePath;
			if (EncoderPath.EndsWith(TEXT("ffmpeg.exe")))
			{
				EncoderPath.ReplaceInline(TEXT("ffmpeg.exe"), TEXT("ffprobe.exe"));
				Result = ExtractInfoUsingFFProbe(InFilePath, EncoderPath);
			}
		}
	}

	if (Result.HasValue())
	{
		UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Extracted timecode '%s' from video file: %s"), *TimecodeInfo.Timecode.ToString(), *FilePath);
	}
	else
	{
		UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Timecode not found for video file: %s"), *FilePath);
	}

	return Result;
}

FCaptureExtractVideoInfo::FExtractResult FCaptureExtractVideoInfo::ExtractInfoUsingElectraPlayer(const FString& InFilePath)
{
	using namespace UE::CaptureManager;

	using FEventSinkResult = FExtractResult;
	TPromise<FExtractResult> EventSinkPromise;

	class FMediaEventSink : public IMediaEventSink
	{
	public:

		FMediaEventSink(TPromise<FEventSinkResult>& InPromise)
			: Promise(InPromise)
			, bReleased(false)
		{
		}

		virtual void ReceiveMediaEvent(EMediaEvent Event) override
		{
			if (bReleased)
			{
				return;
			}

			if (Event == EMediaEvent::MediaOpened)
			{
				Promise.SetValue(MakeValue());
			}
			else if (Event == EMediaEvent::MediaOpenFailed)
			{
				Promise.SetValue(MakeError(ECaptureExtractInfoError::UnableToOpenMedia));
			}
		}

		void Release()
		{
			bReleased = true;
		}

	private:

		TPromise<FEventSinkResult>& Promise;
		std::atomic_bool bReleased;

	} MediaEventSink(EventSinkPromise);

	IElectraPlayerPluginModule& ElectraModule = FModuleManager::LoadModuleChecked<IElectraPlayerPluginModule>("ElectraPlayerPlugin");

	TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> MediaPlayer = ElectraModule.CreatePlayer(MediaEventSink);
	if (!MediaPlayer)
	{
		// Broken promises are considered errors
		FExtractResult Error = MakeError(ECaptureExtractInfoError::InternalError);
		EventSinkPromise.SetValue(Error);
		return Error;
	}

	UDesiredPlayerMediaSource* MediaSource = NewObject<UDesiredPlayerMediaSource>();
	MediaSource->SetFilePath(InFilePath);

	FString FileUrl = TEXT("file://") + InFilePath;

	FMediaPlayerOptions PlayerOptions;
	PlayerOptions.SetAllAsOptional();
	PlayerOptions.InternalCustomOptions.Emplace(MediaPlayerOptionValues::ParseTimecodeInfo(), FVariant());

	MediaPlayer->Open(FileUrl, MediaSource, &PlayerOptions);

	FString TimecodeString;
	FString TimecodeRateString;

	FDateTime EventWaitStart = FDateTime::Now();

	TFuture<FEventSinkResult> EventSinkFuture = EventSinkPromise.GetFuture();

	bool bIsTimeout = true;

	bool bFoundTimecode = false;
	bool bFoundFrameRate = false;

	while ((FDateTime::Now() - EventWaitStart).GetSeconds() < TimeoutPeriod)
	{
		MediaPlayer->TickInput(0, 0);

		if (EventSinkFuture.WaitFor(FTimespan::FromMilliseconds(100.0f))) // Wait 100 milliseconds before invoking TickInput again
		{
			FEventSinkResult Result = EventSinkFuture.Get();
			if (Result.HasError())
			{
				return Result;
			}

			bIsTimeout = false;

			const FVariant Timecode = MediaPlayer->GetMediaInfo(UMediaPlayer::MediaInfoNameStartTimecodeValue.Resolve());
			if (!Timecode.IsEmpty())
			{
				TimecodeString = Timecode.GetValue<FString>();

				const FVariant TimecodeRate = MediaPlayer->GetMediaInfo(UMediaPlayer::MediaInfoNameStartTimecodeFrameRate.Resolve());
				if (!TimecodeRate.IsEmpty())
				{
					TimecodeRateString = TimecodeRate.GetValue<FString>();
				}
			}

			int32 Track = MediaPlayer->GetTracks().GetSelectedTrack(EMediaTrackType::Video);
			int32 Format = MediaPlayer->GetTracks().GetTrackFormat(EMediaTrackType::Video, Track);

			if (Track != INDEX_NONE && Format != INDEX_NONE)
			{
				FMediaVideoTrackFormat FormatInfo;
				MediaPlayer->GetTracks().GetVideoTrackFormat(Track, Format, FormatInfo);

				FrameRate = ConvertFrameRate(FormatInfo.FrameRate);
				bFoundFrameRate = true;
			}

			break;
		}
	}

	// Making sure that no additional events will arrive
	MediaEventSink.Release();
	MediaPlayer->Close();

	if (bIsTimeout)
	{
		FExtractResult Error = MakeError(ECaptureExtractInfoError::InternalError);

		if (!EventSinkFuture.IsReady())
		{
			// Broken promises are considered errors
			EventSinkPromise.SetValue(Error);
		}

		return Error;
	}

	if (!TimecodeString.IsEmpty())
	{
		const TOptional<FTimecode> Timecode = FTimecode::ParseTimecode(*TimecodeString);
		if (!Timecode.IsSet())
		{
			UE_LOG(LogCaptureExtractTimecode, Warning, TEXT("Failed to parse the timecode from %s"), *InFilePath);
		}
		else
		{
			TimecodeInfo.Timecode = Timecode.GetValue();
			bFoundTimecode = true;
		}
	}
	else
	{
		UE_LOG(LogCaptureExtractTimecode, Warning, TEXT("Timecode has not been found in %s"), *InFilePath);
	}

	bool bFoundTimecodeRate = false;
	if (!TimecodeRateString.IsEmpty())
	{
		TValueOrError<FFrameRate, UE::CaptureManager::ECaptureExtractInfoError> Result = ParseTimecodeRate(TimecodeRateString);
		if (Result.IsValid())
		{
			TimecodeInfo.TimecodeRate = Result.GetValue();
			bFoundTimecodeRate = true;
		}
	}
	else
	{
		if (bFoundFrameRate)
		{
			// We didn't extract timecode rate, but we did find the frame rate from the video, so use that instead
			UE_LOG(LogCaptureExtractTimecode, Warning, TEXT("Timecode rate has not been found for %s. Using video frame rate '%.2f' as timecode rate"), *InFilePath, FrameRate.AsDecimal());
			TimecodeInfo.TimecodeRate = FrameRate;
		}
		else
		{
			UE_LOG(LogCaptureExtractTimecode, Warning, TEXT("Timecode rate has not been found for %s. Using default frame rate '%.2f' as timecode rate"), *InFilePath, FFrameRate().AsDecimal());
		}
	}

	if (!bFoundTimecodeRate && !bFoundFrameRate)
	{
		return MakeError(ECaptureExtractInfoError::TimecodeRateNotFound);
	}

	if (!bFoundTimecode)
	{
		if (!bFoundFrameRate)
		{
			// Both not found - InternalError as an new enum could not be added in a hotfix release
			return MakeError(ECaptureExtractInfoError::InternalError);
		}
		else
		{
			return MakeError(ECaptureExtractInfoError::TimecodeNotFound);
		}
	}

	return MakeValue();
}

FCaptureExtractVideoInfo::FExtractResult FCaptureExtractVideoInfo::ExtractInfoUsingFFProbe(const FString& InFilePath, const FString& InFFProbePath)
{
	using namespace UE::CaptureManager;

	FString CommandArgs = FString::Format(TEXT("-v error -select_streams v:0 -show_entries stream_tags=timecode:stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 \"{0}\""), { InFilePath });

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	verify(FPlatformProcess::CreatePipe(ReadPipe, WritePipe, false));

	constexpr bool bLaunchDetached = false;
	constexpr bool bLaunchHidden = true;
	constexpr bool bLaunchReallyHidden = true;
	FProcHandle ProcHandle =
		FPlatformProcess::CreateProc(*InFFProbePath, *CommandArgs, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, nullptr, WritePipe, nullptr);

	ON_SCOPE_EXIT
	{
		FPlatformProcess::TerminateProc(ProcHandle);
		FPlatformProcess::CloseProc(ProcHandle);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	};

	if (!ProcHandle.IsValid())
	{
		UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Extract timecode: Failed to start the process %s %s"), *InFFProbePath, *CommandArgs);
		return MakeError(ECaptureExtractInfoError::InternalError);
	}

	const FDateTime WaitStart = FDateTime::Now();

	TArray<uint8> FullCommandOutput;
	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		TArray<uint8> CommandOutput;
		{
			bool Read = FPlatformProcess::ReadPipeToArray(ReadPipe, CommandOutput);
			if (!Read)
			{
				CommandOutput.Empty();
			}
		}

		if ((FDateTime::Now() - WaitStart).GetSeconds() > TimeoutPeriod)
		{
			// Timed out
			break;
		}

		if (CommandOutput.IsEmpty())
		{
			FPlatformProcess::Sleep(0.1f);
			continue;
		}

		FullCommandOutput.Append(MoveTemp(CommandOutput));

	}

	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);

	TArray<uint8> CommandOutput;
	{
		bool Read = FPlatformProcess::ReadPipeToArray(ReadPipe, CommandOutput);
		if (!Read)
		{
			CommandOutput.Empty();
		}
	}
	FullCommandOutput.Append(CommandOutput);

	if (ReturnCode != 0)
	{
		if (!FullCommandOutput.IsEmpty())
		{
			UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Failed to run the command: %s %s"), *InFFProbePath, *CommandArgs);

			FString CommandOutputStr = FString::ConstructFromPtrSize(reinterpret_cast<const UTF8CHAR*>(FullCommandOutput.GetData()), FullCommandOutput.Num());
			UE_LOG(LogCaptureExtractTimecode, Display,
				   TEXT("Output from the command:\n>>>>>>\n%s<<<<<<"), *CommandOutputStr);
		}

		return MakeError(ECaptureExtractInfoError::InternalError);
	}
	bool bFoundTimecode = false;
	bool bFoundFrameRate = false;

	FString CommandOutputStr = FString::ConstructFromPtrSize(reinterpret_cast<const UTF8CHAR*>(FullCommandOutput.GetData()), FullCommandOutput.Num());

	// Split the timecode and frame rate lines from the output
	TArray<FString> OutputLinesStr;
	CommandOutputStr.ParseIntoArrayLines(OutputLinesStr);

	for (const FString& Line : OutputLinesStr)
	{
		if (!bFoundFrameRate)
		{
			TArray<FString> FrameRatePartsStr;
			Line.ParseIntoArray(FrameRatePartsStr, TEXT("/"));
			if (FrameRatePartsStr.Num() == 2)
			{
				int32 Numerator = FCString::Atoi(*FrameRatePartsStr[0]);
				int32 Denominator = FCString::Atoi(*FrameRatePartsStr[1]);

				FrameRate = FFrameRate(Numerator, Denominator);
				bFoundFrameRate = true;
				continue;
			}
		}

		if (!bFoundTimecode)
		{
			TArray<FString> TimecodePartsStr;
			Line.ParseIntoArray(TimecodePartsStr, TEXT(":")); // We don't support DF timecode
			if (TimecodePartsStr.Num() == 4)
			{
				int32 Hours = FCString::Atoi(*TimecodePartsStr[0]);
				int32 Minutes = FCString::Atoi(*TimecodePartsStr[1]);
				int32 Seconds = FCString::Atoi(*TimecodePartsStr[2]);
				int32 Frames = FCString::Atoi(*TimecodePartsStr[3]);

				constexpr bool bIsDropFrame = false;
				TimecodeInfo.Timecode = FTimecode(Hours, Minutes, Seconds, Frames, bIsDropFrame);
				bFoundTimecode = true;
				continue;
			}
		}
	}

	if (bFoundFrameRate)
	{
		// We didn't extract timecode rate, but we did find the frame rate from the video, so use that instead
		UE_LOG(LogCaptureExtractTimecode, Warning, TEXT("Timecode rate has not been found. Using video frame rate '%.2f' as timecode rate"), FrameRate.AsDecimal());
		TimecodeInfo.TimecodeRate = FrameRate;
	}

	return MakeValue();
}

TValueOrError<FFrameRate, UE::CaptureManager::ECaptureExtractInfoError> FCaptureExtractVideoInfo::ParseTimecodeRate(FString TimecodeRateString)
{
	using namespace UE::CaptureManager;

	if (TimecodeRateString.IsEmpty())
	{
		return MakeError(ECaptureExtractInfoError::TimecodeRateNotFound);
	}
	else
	{
		// TimecodeRateString is made out of FFrameRate::ToPrettyText()
		// It could be "{0} fps" or "{0} s"

		FFrameRate TimecodeRate;

		FString Left;
		FString Right;
		TimecodeRateString.Split(" ", &Left, &Right);

		double Number = FCString::Atoi(*Left);
		uint32 IntNumber;
		uint32 Multiplier = 1;


		for (; Multiplier <= 10000; Multiplier *= 10)
		{
			double TmpNumber = Number * Multiplier;
			IntNumber = FMath::RoundToInt32(TmpNumber);
			if (FMath::Abs(TmpNumber - IntNumber) < 0.01)
			{
				break;
			}
		}

		uint32 Nominator = 0;
		uint32 Denominator = 0;

		if (Right == "fps")
		{
			Nominator = IntNumber;
			Denominator = Multiplier;
		}
		else if (Right == "s")
		{
			Nominator = Multiplier;
			Denominator = IntNumber;
		}

		TimecodeRate = FFrameRate(Nominator, Denominator);

		return MakeValue(TimecodeRate);
	}
}

FCaptureExtractAudioTimecode::FCaptureExtractAudioTimecode(const FString& InFilePath)
	: FilePath(InFilePath)
{}

FCaptureExtractAudioTimecode::FTimecodeInfoResult FCaptureExtractAudioTimecode::Extract()
{
	return ExtractTimecodeFromBroadcastWaveFormat(FFrameRate());
}

static FFrameRate EstimateSmpteTimecodeRate(const FFrameRate InMediaFrameRate)
{
	if (FMath::IsNearlyEqual(InMediaFrameRate.AsDecimal(), 60.0))
	{
		return FFrameRate(30'000, 1'000);
	}

	if (FMath::IsNearlyEqual(InMediaFrameRate.AsDecimal(), 59.94))
	{
		// 29.97
		return FFrameRate(30'000, 1'001);
	}

	if (FMath::IsNearlyEqual(InMediaFrameRate.AsDecimal(), 50.0))
	{
		FFrameRate(25'000, 1'000);
	}

	if (FMath::IsNearlyEqual(InMediaFrameRate.AsDecimal(), 48.0))
	{
		FFrameRate(24'000, 1'000);
	}

	return InMediaFrameRate;
}

FCaptureExtractAudioTimecode::FTimecodeInfoResult FCaptureExtractAudioTimecode::Extract(FFrameRate InFrameRate)
{
	using namespace UE::CaptureManager;

	check(!FilePath.IsEmpty());

	FTimecodeInfoResult Result = MakeError(ECaptureExtractInfoError::UnhandledMedia);

	FString FileExtension = FPaths::GetExtension(FilePath);
	if (FileExtension == "wav")
	{
		// Convert timecode rate to SMPTE timecode rate
		FFrameRate TimecodeRate = EstimateSmpteTimecodeRate(InFrameRate);
		Result = ExtractTimecodeFromBroadcastWaveFormat(TimecodeRate);
	}

	if (Result.HasValue())
	{
		UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Extracted timecode '%s' from audio file: %s"), *Result.GetValue().Timecode.ToString(), *FilePath);
	}
	else
	{
		UE_LOG(LogCaptureExtractTimecode, Display, TEXT("Timecode not found for audio file: %s"), *FilePath);
	}

	return Result;
}

FCaptureExtractAudioTimecode::FTimecodeInfoResult FCaptureExtractAudioTimecode::ExtractTimecodeFromBroadcastWaveFormat(FFrameRate InTimecodeRate)
{
	using namespace UE::CaptureManager;

	TArray<uint8> WAVData;
	if (FFileHelper::LoadFileToArray(WAVData, *FilePath))
	{
		FWaveModInfo WAVInfo;
		if (WAVInfo.ReadWaveInfo(WAVData.GetData(), WAVData.Num()))
		{
			if (WAVInfo.TimecodeInfo.IsValid())
			{
				FSoundWaveTimecodeInfo TimecodeInfo = *WAVInfo.TimecodeInfo.Get();
				const double NumSecondsSinceMidnight = TimecodeInfo.GetNumSecondsSinceMidnight();

				FFrameRate TimecodeRate = TimecodeInfo.TimecodeRate;
				const bool bTimecodeRateIsSampleRate = TimecodeRate == FFrameRate(TimecodeInfo.NumSamplesPerSecond, 1);
				if (bTimecodeRateIsSampleRate)
				{
					UE_LOG(
						LogCaptureExtractTimecode,
						Display,
						TEXT(
							"Embedded timecode rate is %.2f fps (the sample rate). "
							"This usually indicates there is no timecode rate information in the wav file: %s"
						),
						TimecodeInfo.TimecodeRate.AsDecimal(),
						*FilePath
					);

					if (InTimecodeRate != FFrameRate())
					{
						// Use the provided timecode rate instead
						TimecodeRate = InTimecodeRate;

						UE_LOG(
							LogCaptureExtractTimecode,
							Display,
							TEXT(
								"Taking the embedded audio timecode but estimating an SMPTE audio timecode rate. "
								"Timecode rate for %s set to %.2f"
							),
							*FilePath,
							TimecodeRate.AsDecimal()
						);
					}
				}

				FTimecode AudioTimecode = FTimecode(NumSecondsSinceMidnight, TimecodeRate, TimecodeInfo.bTimecodeIsDropFrame, /* InbRollover = */ true);
				FTimecodeInfo TimecodeAndRate
				{ 
					.Timecode = MoveTemp(AudioTimecode), 
					.TimecodeRate = MoveTemp(TimecodeRate)
				};

				return MakeValue(MoveTemp(TimecodeAndRate));
			}
		}
	}

	return MakeError(ECaptureExtractInfoError::TimecodeNotFound);
}

#undef LOCTEXT_NAMESPACE
