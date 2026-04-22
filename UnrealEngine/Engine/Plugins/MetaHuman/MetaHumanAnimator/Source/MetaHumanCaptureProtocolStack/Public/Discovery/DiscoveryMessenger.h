// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Discovery/Communication/DiscoveryCommunication.h"
#include "Discovery/Communication/DiscoveryPacket.h"

#include "Discovery/Messages/DiscoveryRequest.h"
#include "Discovery/Messages/DiscoveryResponse.h"
#include "Discovery/Messages/DiscoveryNotify.h"

#include "Utility/Error.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")FDiscoveryMessenger final
{
public:

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    DECLARE_DELEGATE_OneParam(FOnResponseArrived, FDiscoveryResponse InResponse);
    DECLARE_DELEGATE_OneParam(FOnNotifyArrived, FDiscoveryNotify InResponse);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

    UE_API FDiscoveryMessenger();
    UE_API ~FDiscoveryMessenger();

    UE_API TProtocolResult<void> Start();
    UE_API TProtocolResult<void> Stop();

    UE_API TProtocolResult<void> SendMulticastRequest();

    UE_API void SetResponseHandler(FOnResponseArrived InOnResponse);
    UE_API void SetNotifyHandler(FOnNotifyArrived InOnNotify);

private:

    UE_API void OnPacketArrived(FDiscoveryPacket InPacket);

    FDiscoveryCommunication Communication;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    FOnResponseArrived OnResponse;
    FOnNotifyArrived OnNotify;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

#undef UE_API
