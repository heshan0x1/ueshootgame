// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Discovery/Communication/DiscoveryPacket.h"

#include "Utility/Error.h"

#include "Containers/Array.h"
#include "Containers/StaticArray.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FDiscoveryNotify
{
public:

	static UE_API const uint32 MinPayloadSize;

    using FServerId = TStaticArray<uint8, 16>;

    enum class EConnectionState : uint8
    {
        Offline = 0,
        Online = 1,

        Invalid
    };

    UE_API FDiscoveryNotify(FServerId InServerId, uint16 InControlPort, EConnectionState InConnectionState, TArray<uint16> InSupportedVersions);

    static UE_API TProtocolResult<FDiscoveryNotify> Deserialize(const FDiscoveryPacket& InPacket);
    static UE_API TProtocolResult<FDiscoveryPacket> Serialize(const FDiscoveryNotify& InNotify);

    UE_API const FServerId& GetServerId() const;
    UE_API uint16 GetControlPort() const;
    UE_API EConnectionState GetConnectionState() const;
	UE_API const TArray<uint16>& GetSupportedVersions() const;

private:

    static UE_API EConnectionState ToConnectionState(uint8 InConnectionState);
    static UE_API uint8 FromConnectionState(EConnectionState InConnectionState);

    FServerId ServerId;
    uint16 ControlPort;
    EConnectionState ConnectionState;
	TArray<uint16> SupportedVersions;
};

#undef UE_API
