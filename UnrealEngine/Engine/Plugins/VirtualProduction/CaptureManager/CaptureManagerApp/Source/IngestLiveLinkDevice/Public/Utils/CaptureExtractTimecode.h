// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FileMediaSource.h"

#include "MediaSample.h"
#include "MediaPlayer.h"

#include "Templates/ValueOrError.h"

#include "IMediaClock.h"
#include "IMediaClockSink.h"
#include "IMediaModule.h"

#include "CaptureExtractTimecode.generated.h"

#define UE_API INGESTLIVELINKDEVICE_API

namespace UE::CaptureManager
{

struct FTimecodeInfo
{
	FTimecode Timecode;
	FFrameRate TimecodeRate;
};

struct FVideoInformation
{
	FFrameRate FrameRate;
	FTimecodeInfo TimecodeInfo;
};

enum ECaptureExtractInfoError : int32
{
	InternalError = -1,
	TimecodeNotFound = 1,
	UnableToParseTimecode = 2,
	UnableToParseTimecodeRate = 3,
	TimecodeRateNotFound = 4,
	UnhandledMedia = 5,
	UnableToOpenMedia = 6,
};
}

#if WITH_EDITOR
UCLASS(BlueprintType, MinimalAPI)
class UDesiredPlayerMediaSource : public UFileMediaSource
{
	GENERATED_BODY()
public:
	//~ IMediaOptions interface
	virtual FName GetDesiredPlayerName() const override
	{
		return TEXT("ElectraPlayer");
	}
};
#endif // WITH_EDITOR

class FCaptureExtractVideoInfo final
{
public:

	using FResult = TValueOrError<FCaptureExtractVideoInfo, UE::CaptureManager::ECaptureExtractInfoError>;

	UE_API static FResult Create(const FString& InFilePath);

	UE_API FFrameRate GetFrameRate() const;
	UE_API FTimecode GetTimecode() const;
	UE_API FFrameRate GetTimecodeRate() const;

private:

	FCaptureExtractVideoInfo();

	using FExtractResult = TValueOrError<void, UE::CaptureManager::ECaptureExtractInfoError>;
	FExtractResult ExtractInfo(const FString& InFilePath);
	FExtractResult ExtractInfoUsingElectraPlayer(const FString& InFilePath);
	FExtractResult ExtractInfoUsingFFProbe(const FString& InFilePath, const FString& InEncoderPath);

	TValueOrError<FFrameRate, UE::CaptureManager::ECaptureExtractInfoError> ParseTimecodeRate(FString TimecodeRateString);

	static constexpr int32 TimeoutPeriod = 3;

	FFrameRate FrameRate;
	UE::CaptureManager::FTimecodeInfo TimecodeInfo;
};

class FCaptureExtractAudioTimecode final : public TSharedFromThis<FCaptureExtractAudioTimecode>
{
public:

	using FTimecodeInfoResult = TValueOrError<UE::CaptureManager::FTimecodeInfo, UE::CaptureManager::ECaptureExtractInfoError>;

	UE_API FCaptureExtractAudioTimecode(const FString& InFilePath);
	UE_API ~FCaptureExtractAudioTimecode() = default;

	UE_API FTimecodeInfoResult Extract();
	UE_API FTimecodeInfoResult Extract(FFrameRate InFrameRate);

private:

	FString FilePath;

	const int32 TimeoutPeriod = 3;

	FTimecodeInfoResult ExtractTimecodeFromBroadcastWaveFormat(FFrameRate InTimecodeRate);
};

#undef UE_API