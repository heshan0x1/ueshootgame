// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaCaptureSupport.h"
#include "ILiveLinkSource.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MetaHumanLocalLiveLinkSourceBlueprint.generated.h"



USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkVideoDevice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Url;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	bool IsMediaBundle = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkVideoTrack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY()
	int32 Index = -1;

	UPROPERTY()
	FMetaHumanLiveLinkVideoDevice VideoDevice;
};

USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkVideoFormat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FIntPoint Resolution = FIntPoint(0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	float FrameRate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY()
	int32 Index = -1;

	UPROPERTY()
	FMetaHumanLiveLinkVideoTrack VideoTrack;
};

// The audio device, track and format classes are very similar to the video cases.
// We could use a generic device class that would technically suffice for both audio
// and video cases. However, this would potentially complicate the actual Blueprint
// using these classes and too easily allow for accidental mixing of audio and video cases. 
// We want to ensure a clear distinction between audio and video cases which we achieve
// by using distinct classes for each.

USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkAudioDevice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Url;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	bool IsMediaBundle = false;
};

USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkAudioTrack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY()
	int32 Index = -1;

	UPROPERTY()
	FMetaHumanLiveLinkAudioDevice AudioDevice;
};

USTRUCT(BlueprintType)
struct FMetaHumanLiveLinkAudioFormat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	int32 NumChannels = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	int32 SampleRate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "MetaHuman Live Link")
	FString Name;

	UPROPERTY()
	int32 Index = -1;

	UPROPERTY()
	FMetaHumanLiveLinkAudioTrack AudioTrack;
};

UCLASS(Blueprintable)
class METAHUMANLOCALLIVELINKSOURCE_API UMetaHumanLocalLiveLinkSourceBlueprint : public UBlueprintFunctionLibrary
{

public:

	GENERATED_BODY()

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetVideoDevices(TArray<FMetaHumanLiveLinkVideoDevice>& VideoDevices, bool IncludeMediaBundles = true);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetVideoTracks(const FMetaHumanLiveLinkVideoDevice& VideoDevice, TArray<FMetaHumanLiveLinkVideoTrack>& VideoTracks, bool& TimedOut, float Timeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetVideoFormats(const FMetaHumanLiveLinkVideoTrack& VideoTrack, TArray<FMetaHumanLiveLinkVideoFormat>& VideoFormats, bool& TimedOut, bool FilterFormats = true, float Timeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void CreateVideoSource(FLiveLinkSourceHandle& VideoSource, bool& Succeeded);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void CreateVideoSubject(const FLiveLinkSourceHandle& VideoSource, const FMetaHumanLiveLinkVideoFormat& VideoFormat, const FString& SubjectName, FLiveLinkSubjectKey& VideoSubject, bool& Succeeded, float StartTimeout = 5, float FormatWaitTime = 0.1, float SampleTimeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetAudioDevices(TArray<FMetaHumanLiveLinkAudioDevice>& AudioDevices, bool IncludeMediaBundles = true);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetAudioTracks(const FMetaHumanLiveLinkAudioDevice& AudioDevice, TArray<FMetaHumanLiveLinkAudioTrack>& AudioTracks, bool& TimedOut, float Timeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetAudioFormats(const FMetaHumanLiveLinkAudioTrack& AudioTrack, TArray<FMetaHumanLiveLinkAudioFormat>& AudioFormats, bool& TimedOut, float Timeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void CreateAudioSource(FLiveLinkSourceHandle& AudioSource, bool& Succeeded);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void CreateAudioSubject(const FLiveLinkSourceHandle& AudioSource, const FMetaHumanLiveLinkAudioFormat& AudioFormat, const FString& SubjectName, FLiveLinkSubjectKey& AudioSubject, bool& Succeeded, float StartTimeout = 5, float FormatWaitTime = 0.1, float SampleTimeout = 5);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman Live Link")
	static void GetSubjectSettings(const FLiveLinkSubjectKey& Subject, UObject*& Settings);
};
