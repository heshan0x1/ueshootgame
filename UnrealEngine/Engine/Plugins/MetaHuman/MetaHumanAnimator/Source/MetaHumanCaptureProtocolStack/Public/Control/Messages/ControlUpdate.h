// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ControlJsonUtilities.h"

#include "Utility/Error.h"

#include "Delegates/Delegate.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class FControlUpdate;
class FControlUpdateCreator final
{
public:
    static TProtocolResult<TSharedRef<FControlUpdate>> Create(const FString& InAddressPath);
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FControlUpdate
{
public:

    DECLARE_DELEGATE_OneParam(FOnUpdateMessage, TSharedPtr<FControlUpdate> InUpdateMessage);

    UE_API FControlUpdate(FString InAddressPath);
    virtual ~FControlUpdate() = default;

    UE_API const FString& GetAddressPath() const;

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody);

private:

    FString AddressPath;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FSessionStopped final : public FControlUpdate
{
public:

    UE_API FSessionStopped();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FRecordingStatusUpdate final : public FControlUpdate
{
public:

    UE_API FRecordingStatusUpdate();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API bool IsRecording() const;

private:

    bool bIsRecording;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FBaseTakeUpdate : public FControlUpdate
{
public:

	using FControlUpdate::FControlUpdate;

	UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

	UE_API const FString& GetTakeName() const;

private:

	FString TakeName;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FTakeAddedUpdate final : public FBaseTakeUpdate
{
public:

	UE_API FTakeAddedUpdate();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FTakeRemovedUpdate final : public FBaseTakeUpdate
{
public:

	UE_API FTakeRemovedUpdate();
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FTakeUpdatedUpdate final : public FBaseTakeUpdate
{
public:

	UE_API FTakeUpdatedUpdate();
};

// IOS

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FDiskCapacityUpdate final : public FControlUpdate
{
public:

    UE_API FDiskCapacityUpdate();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API uint64 GetTotal() const;
    UE_API uint64 GetRemaining() const;

private:

    uint64 Total;
    uint64 Remaining;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FBatteryPercentageUpdate final : public FControlUpdate
{
public:

    UE_API FBatteryPercentageUpdate();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API float GetLevel() const;

private:

    float Level;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") FThermalStateUpdate final : public FControlUpdate
{
public:

    enum class EState
    {
        Nominal = 0,
        Fair,
        Serious,
        Critical,

        Invalid
    };

    UE_API FThermalStateUpdate();

    UE_API virtual TProtocolResult<void> Parse(TSharedPtr<FJsonObject> InBody) override;

    UE_API EState GetState() const;

private:

    static UE_API EState ConvertState(const FString& InStateString);

    EState State;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef UE_API
