// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlResponse.h"
#include "Misc/Optional.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FControlRequest
{
public:

    UE_API FControlRequest(FString InAddressPath);
    virtual ~FControlRequest() = default;

    UE_API const FString& GetAddressPath() const;
    UE_API virtual TSharedPtr<FJsonObject> GetBody() const;

private:

    FString AddressPath;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FKeepAliveRequest final : public FControlRequest
{
public:

    using ResponseType = FKeepAliveResponse;

    UE_API FKeepAliveRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStartSessionRequest final : public FControlRequest
{
public:

    using ResponseType = FStartSessionResponse;

    UE_API FStartSessionRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStopSessionRequest final : public FControlRequest
{
public:

    using ResponseType = FStopSessionResponse;

    UE_API FStopSessionRequest();
};


class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetServerInformationRequest final : public FControlRequest
{
public:

    using ResponseType = FGetServerInformationResponse;

    UE_API FGetServerInformationRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FSubscribeRequest final : public FControlRequest
{
public:

    using ResponseType = FSubscribeResponse;

    UE_API FSubscribeRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FUnsubscribeRequest final : public FControlRequest
{
public:

    using ResponseType = FUnsubscribeResponse;

    UE_API FUnsubscribeRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetStateRequest final : public FControlRequest
{
public:

    using ResponseType = FGetStateResponse;

    UE_API FGetStateRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStartRecordingTakeRequest final : public FControlRequest
{
public:

    using ResponseType = FStartRecordingTakeResponse;

    UE_API FStartRecordingTakeRequest(FString InSlateName, 
                               uint16 InTakeNumber, 
                               TOptional<FString> InSubject = TOptional<FString>(),
							   TOptional<FString> InScenario = TOptional<FString>(),
                               TOptional<TArray<FString>> InTags = TOptional<TArray<FString>>());

    UE_API virtual TSharedPtr<FJsonObject> GetBody() const override;

private:

    FString SlateName;
    uint16 TakeNumber;
	TOptional<FString> Subject;
	TOptional<FString> Scenario;
	TOptional<TArray<FString>> Tags;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FStopRecordingTakeRequest final : public FControlRequest
{
public:

    using ResponseType = FStopRecordingTakeResponse;

    UE_API FStopRecordingTakeRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FAbortRecordingTakeRequest final : public FControlRequest
{
public:

	using ResponseType = FAbortRecordingTakeResponse;

	UE_API FAbortRecordingTakeRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetTakeListRequest final : public FControlRequest
{
public:

    using ResponseType = FGetTakeListResponse;

    UE_API FGetTakeListRequest();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FGetTakeMetadataRequest final : public FControlRequest
{
public:

    using ResponseType = FGetTakeMetadataResponse;

    UE_API FGetTakeMetadataRequest(TArray<FString> InNames);

    UE_API virtual TSharedPtr<FJsonObject> GetBody() const override;

private:

    TArray<FString> Names;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef UE_API
