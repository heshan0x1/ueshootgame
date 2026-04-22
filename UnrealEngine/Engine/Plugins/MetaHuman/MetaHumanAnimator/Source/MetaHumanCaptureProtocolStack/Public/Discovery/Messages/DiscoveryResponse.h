// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Discovery/Communication/DiscoveryPacket.h"

#include "Utility/Error.h"

#include "Containers/Array.h"
#include "Containers/StaticArray.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FDiscoveryResponse
{
public:

	static UE_API const uint32 MinPayloadSize;

    using FServerId = TStaticArray<uint8, 16>;

    UE_API FDiscoveryResponse(FServerId InServerId, uint16 InControlPort, TArray<uint16> InSupportedVersions);

    static UE_API TProtocolResult<FDiscoveryResponse> Deserialize(const FDiscoveryPacket& InPacket);
    static UE_API TProtocolResult<FDiscoveryPacket> Serialize(const FDiscoveryResponse& InResponse);

    UE_API const FServerId& GetServerId() const;
    UE_API uint16 GetControlPort() const;
	UE_API const TArray<uint16>& GetSupportedVersions() const;

private:

    FServerId ServerId;
    uint16 ControlPort;
	TArray<uint16> SupportedVersions;
};

#undef UE_API
