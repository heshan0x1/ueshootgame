// Copyright Epic Games, Inc. All Rights Reserved.

#include "Discovery/Messages/DiscoveryNotify.h"
#include "Containers/ArrayView.h"

const uint32 FDiscoveryNotify::MinPayloadSize = 16 + 2 + 1 + 2; // ServerId + ControlPort + ConnectionState + Supported Versions (at least 1)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
FDiscoveryNotify::FDiscoveryNotify(FServerId InServerId, uint16 InControlPort, EConnectionState InConnectionState, TArray<uint16> InSupportedVersions)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    : ServerId(MoveTemp(InServerId))
    , ControlPort(InControlPort)
    , ConnectionState(InConnectionState)
	, SupportedVersions(MoveTemp(InSupportedVersions))
{
}


PRAGMA_DISABLE_DEPRECATION_WARNINGS
TProtocolResult<FDiscoveryNotify> FDiscoveryNotify::Deserialize(const FDiscoveryPacket& InPacket)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	if (InPacket.GetMessageType() != FDiscoveryPacket::EMessageType::Notify)	
	{ 
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return FCaptureProtocolError(TEXT("Invalid request arrived"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	TConstArrayView<uint8> Payload = InPacket.GetPayload();

	if (Payload.Num() < MinPayloadSize)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Invalid number of bytes"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    FServerId ServerId;
    FMemory::Memcpy(ServerId.GetData(), Payload.GetData(), ServerId.Num());

    uint16 ControlPort = *reinterpret_cast<const uint16*>(Payload.GetData() + ServerId.Num());
    uint8 ConnectionState = Payload[ServerId.Num() + sizeof(ControlPort)];

    EConnectionState ConnectionStateEnum = ToConnectionState(ConnectionState);

    if (ConnectionStateEnum == EConnectionState::Invalid)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Invalid connection state field"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

	TConstArrayView<uint8> SupportedVersionsBytes = Payload.RightChop(ServerId.Num() + sizeof(ControlPort) + sizeof(ConnectionStateEnum));
	TArray<uint16> SupportedVersions;
	SupportedVersions.Append((uint16*)SupportedVersionsBytes.GetData(), SupportedVersionsBytes.Num() / 2);

    return FDiscoveryNotify(MoveTemp(ServerId), ControlPort, ConnectionStateEnum, MoveTemp(SupportedVersions));
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TProtocolResult<FDiscoveryPacket> FDiscoveryNotify::Serialize(const FDiscoveryNotify& InNotify)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    TArray<uint8> Payload;
    Payload.Reserve(InNotify.ServerId.Num() + sizeof(InNotify.ControlPort) + sizeof(InNotify.ConnectionState) + InNotify.SupportedVersions.Num() * sizeof(uint16));

    Payload.Append(InNotify.ServerId.GetData(), InNotify.ServerId.Num());
    Payload.Append((uint8*) &InNotify.ControlPort, sizeof(InNotify.ControlPort));

    uint8 ConnectionState = FromConnectionState(InNotify.ConnectionState);

    if (ConnectionState == MAX_uint8)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Invalid connection state field"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    Payload.Add(ConnectionState);
	Payload.Append((uint8*) InNotify.SupportedVersions.GetData(), InNotify.SupportedVersions.Num() * sizeof(uint16));

    return FDiscoveryPacket(FDiscoveryPacket::EMessageType::Notify, MoveTemp(Payload));
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const FDiscoveryNotify::FServerId& FDiscoveryNotify::GetServerId() const
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    return ServerId;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
uint16 FDiscoveryNotify::GetControlPort() const
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    return ControlPort;
}

FDiscoveryNotify::EConnectionState FDiscoveryNotify::GetConnectionState() const
{
    return ConnectionState;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const TArray<uint16>& FDiscoveryNotify::GetSupportedVersions() const
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	return SupportedVersions;
}

FDiscoveryNotify::EConnectionState FDiscoveryNotify::ToConnectionState(uint8 InConnectionState)
{
    if (InConnectionState == 0)
    {
        return EConnectionState::Offline;
    }
    else if (InConnectionState == 1)
    {
        return EConnectionState::Online;
    }
    else
    {
        return EConnectionState::Invalid;
    }
}

uint8 FDiscoveryNotify::FromConnectionState(EConnectionState InConnectionState)
{
    switch (InConnectionState)
    {
        case EConnectionState::Offline:
            return 0;
        case EConnectionState::Online:
            return 1;
        case EConnectionState::Invalid:
        default:
            return MAX_uint8;
    }
}
