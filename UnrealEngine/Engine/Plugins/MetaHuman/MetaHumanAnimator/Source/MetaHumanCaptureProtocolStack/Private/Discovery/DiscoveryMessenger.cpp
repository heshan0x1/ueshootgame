// Copyright Epic Games, Inc. All Rights Reserved.

#include "Discovery/DiscoveryMessenger.h"

DEFINE_LOG_CATEGORY(LogCPSDiscoveryMessenger)

FDiscoveryMessenger::FDiscoveryMessenger()
{
}

FDiscoveryMessenger::~FDiscoveryMessenger()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Stop();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TProtocolResult<void> FDiscoveryMessenger::Start()
{
    CPS_CHECK_VOID_RESULT(Communication.Start());
    Communication.SetReceiveHandler(
        FDiscoveryCommunication::FOnPacketReceived::CreateRaw(this, &FDiscoveryMessenger::OnPacketArrived));

    return ResultOk;
}

TProtocolResult<void> FDiscoveryMessenger::Stop()
{
    return Communication.Stop();
}

TProtocolResult<void> FDiscoveryMessenger::SendMulticastRequest()
{
    constexpr int32 NumberOfRequests = 3;

    FDiscoveryRequest Request;
    TProtocolResult<FDiscoveryPacket> SerializeResult = FDiscoveryRequest::Serialize(Request);

    if (SerializeResult.IsError())
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Failed to serialize request"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    for (int32 Counter = 0; Counter < NumberOfRequests; ++Counter)
    {
		FDiscoveryPacket Packet = SerializeResult.GetResult();
        Communication.SendMessage(MoveTemp(Packet), FDiscoveryCommunication::MulticastAddress, FDiscoveryCommunication::MulticastPort);
    }

    return ResultOk;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FDiscoveryMessenger::SetResponseHandler(FOnResponseArrived InOnResponse)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    OnResponse = MoveTemp(InOnResponse);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FDiscoveryMessenger::SetNotifyHandler(FOnNotifyArrived InOnNotify)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    OnNotify = MoveTemp(InOnNotify);
}

void FDiscoveryMessenger::OnPacketArrived(FDiscoveryPacket InPacket)
{
    const TArray<uint8>& Payload = InPacket.GetPayload();

    if (InPacket.GetMessageType() == FDiscoveryPacket::EMessageType::Request)
    {
        UE_LOG(LogCPSDiscoveryMessenger, Error, TEXT("Client currently doesn't support requests."));
        return;
    }
    else if (InPacket.GetMessageType() == FDiscoveryPacket::EMessageType::Response)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        TProtocolResult<FDiscoveryResponse> DeserializeResult = FDiscoveryResponse::Deserialize(InPacket);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

        if (DeserializeResult.IsError())
        {
            UE_LOG(LogCPSDiscoveryMessenger, Error, TEXT("Failed to parse the response message."));
            return;
        }

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        FDiscoveryResponse Response = DeserializeResult.ClaimResult();
        OnResponse.ExecuteIfBound(MoveTemp(Response));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
    else if (InPacket.GetMessageType() == FDiscoveryPacket::EMessageType::Notify)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        TProtocolResult<FDiscoveryNotify> DeserializeResult = FDiscoveryNotify::Deserialize(InPacket);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

        if (DeserializeResult.IsError())
        {
            UE_LOG(LogCPSDiscoveryMessenger, Error, TEXT("Failed to parse the notify message."));
            return;
        }

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        FDiscoveryNotify Notify = DeserializeResult.ClaimResult();
        OnNotify.ExecuteIfBound(MoveTemp(Notify));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
    else
    {
        UE_LOG(LogCPSDiscoveryMessenger, Error, TEXT("Invalid message arrived."));
        return;
    }
}
