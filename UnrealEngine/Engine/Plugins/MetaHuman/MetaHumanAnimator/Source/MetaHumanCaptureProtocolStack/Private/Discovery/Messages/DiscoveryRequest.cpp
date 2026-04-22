// Copyright Epic Games, Inc. All Rights Reserved.

#include "Discovery/Messages/DiscoveryRequest.h"
#include "Containers/ArrayView.h"


const uint32 FDiscoveryRequest::PayloadSize = 0;

TProtocolResult<FDiscoveryRequest> FDiscoveryRequest::Deserialize(const FDiscoveryPacket& InPacket)
{
	if (InPacket.GetMessageType() != FDiscoveryPacket::EMessageType::Request)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return FCaptureProtocolError(TEXT("Invalid request arrived"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

    if (InPacket.GetPayload().Num() != PayloadSize)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Invalid number of bytes"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    return FDiscoveryRequest();
}

TProtocolResult<FDiscoveryPacket> FDiscoveryRequest::Serialize(const FDiscoveryRequest& InRequest)
{
    return FDiscoveryPacket(FDiscoveryPacket::EMessageType::Request, TArray<uint8>());
}
