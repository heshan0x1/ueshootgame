// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlJsonUtilities.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FControlResponse
{
public:

    UE_API FControlResponse(FString InAddressPath);
    virtual ~FControlResponse() = default;

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody);

    UE_API const FString& GetAddressPath() const;

private:

    FString AddressPath;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FKeepAliveResponse final : public FControlResponse
{
public:

    UE_API FKeepAliveResponse();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStartSessionResponse final : public FControlResponse
{
public:
    
    UE_API FStartSessionResponse();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API const FString& GetSessionId() const;

private:

    FString SessionId;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStopSessionResponse final : public FControlResponse
{
public:

    UE_API FStopSessionResponse();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetServerInformationResponse final : public FControlResponse
{
public:
    
    UE_API FGetServerInformationResponse();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API const FString& GetId() const;
    UE_API const FString& GetName() const;
    UE_API const FString& GetModel() const;
    UE_API const FString& GetPlatformName() const;
    UE_API const FString& GetPlatformVersion() const;
    UE_API const FString& GetSoftwareName() const;
    UE_API const FString& GetSoftwareVersion() const;
	UE_API uint16 GetExportPort() const;

private:

    FString Id;
    FString Name;
    FString Model;
    FString PlatformName;
    FString PlatformVersion;
    FString SoftwareName;
    FString SoftwareVersion;
	uint16 ExportPort;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FSubscribeResponse final : public FControlResponse
{
public:

    UE_API FSubscribeResponse();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FUnsubscribeResponse final : public FControlResponse
{
public:

    UE_API FUnsubscribeResponse();
};


class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetStateResponse final : public FControlResponse
{
public:

    UE_API FGetStateResponse();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API bool IsRecording() const;
    UE_API const TSharedPtr<FJsonObject>& GetPlatformState() const;

private:

    bool bIsRecording;
    TSharedPtr<FJsonObject> PlatformState;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStartRecordingTakeResponse final : public FControlResponse
{
public:

    UE_API FStartRecordingTakeResponse();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStopRecordingTakeResponse final : public FControlResponse
{
public:

    UE_API FStopRecordingTakeResponse();

	UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

	UE_API const FString& GetTakeName() const;
private:

	FString TakeName;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FAbortRecordingTakeResponse final : public FControlResponse
{
public:

	UE_API FAbortRecordingTakeResponse();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetTakeListResponse final : public FControlResponse
{
public:

    UE_API FGetTakeListResponse();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API const TArray<FString>& GetNames() const;

private:

    TArray<FString> Names;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetTakeMetadataResponse final : public FControlResponse
{
public:

    struct FFileObject
    {
        FString Name;
        uint64 Length;
    };

    struct FVideoObject
    {
		uint64 Frames;
        uint16 FrameRate;
        uint32 Height;
        uint32 Width;
    };

    struct FAudioObject
    {
        uint8 Channels;
        uint32 SampleRate;
        uint8 BitsPerChannel;
    };

    struct FTakeObject
    {
        FString Name;
        FString Slate;
        uint16 TakeNumber;
        FString DateTime;
        FString AppVersion;
        FString Model;
        FString Subject;
        FString Scenario;
        TArray<FString> Tags;

        TArray<FFileObject> Files;

        FVideoObject Video;
        FAudioObject Audio;
    };

    UE_API FGetTakeMetadataResponse();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API const TArray<FTakeObject>& GetTakes() const;

private:

    UE_API TProtocolResult<void> CreateTakeObject(const TSharedPtr<FJsonObject>& InTakeObject, FTakeObject& OutTake) const;
    UE_API TProtocolResult<void> CreateFileObject(const TSharedPtr<FJsonObject>& InFileObject, FFileObject& OutFile) const;

    TArray<FTakeObject> Takes;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef UE_API
