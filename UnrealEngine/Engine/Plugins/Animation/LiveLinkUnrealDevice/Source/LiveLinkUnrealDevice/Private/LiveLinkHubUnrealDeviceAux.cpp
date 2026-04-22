// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkHubUnrealDeviceAux.h"
#include "Devices/LiveLinkUnrealDevice.h"
#include "Engine/Engine.h"
#include "ILiveLinkHubMessagingModule.h"
#include "LiveLinkUnrealDeviceMessages.h"
#include "Logging/StructuredLog.h"
#include "MessageEndpointBuilder.h"
#include "Recorder/TakeRecorderSubsystem.h"


FLiveLinkHubUnrealDeviceAuxManager::FLiveLinkHubUnrealDeviceAuxManager()
{
	FMessageEndpointBuilder EndpointBuilder = FMessageEndpointBuilder("LiveLinkHubTakeRecorderAuxHandler");
	MessageEndpoint = EndpointBuilder
		.Handling<FLiveLinkTakeRecorderCmd_SetSlateName>(this, &FLiveLinkHubUnrealDeviceAuxManager::HandleSetSlateName)
		.Handling<FLiveLinkTakeRecorderCmd_SetTakeNumber>(this, &FLiveLinkHubUnrealDeviceAuxManager::HandleSetTakeNumber)
		.Handling<FLiveLinkTakeRecorderCmd_StartRecording>(this, &FLiveLinkHubUnrealDeviceAuxManager::HandleStartRecording)
		.Handling<FLiveLinkTakeRecorderCmd_StopRecording>(this, &FLiveLinkHubUnrealDeviceAuxManager::HandleStopRecording)
		.Handling<FLiveLinkHubAuxChannelCloseMessage>(this, &FLiveLinkHubUnrealDeviceAuxManager::HandleAuxClose)
		.ReceivingOnThread(ENamedThreads::GameThread)
		.Build();

	RegisterRequestHandler();
	RegisterTakeRecorderDelegates();
}


FLiveLinkHubUnrealDeviceAuxManager::~FLiveLinkHubUnrealDeviceAuxManager()
{
	ILiveLinkHubMessagingModule* HubMessagingModule = FModuleManager::Get().GetModulePtr<ILiveLinkHubMessagingModule>("LiveLinkHubMessaging");
	if (HubMessagingModule)
	{
		HubMessagingModule->UnregisterAuxChannelRequestHandler<FLiveLinkUnrealDeviceAuxChannelRequestMessage>();
	}

	if (GEngine)
	{
		if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>())
		{
			TakeRecorder->GetOnRecordingStartedEvent().RemoveAll(this);
			TakeRecorder->GetOnRecordingStoppedEvent().RemoveAll(this);
		}
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::RegisterRequestHandler()
{
	ILiveLinkHubMessagingModule& HubMessagingModule =
		FModuleManager::Get().GetModuleChecked<ILiveLinkHubMessagingModule>("LiveLinkHubMessaging");

	HubMessagingModule.RegisterAuxChannelRequestHandler<FLiveLinkUnrealDeviceAuxChannelRequestMessage>(
		[this]
		(
			const FLiveLinkUnrealDeviceAuxChannelRequestMessage& InRequest,
			const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
		)
		{
			UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received Take Recorder channel request from {Sender}",
				InContext->GetSender().ToString());

			const FGuid& ChannelId = InRequest.ChannelId;

			if (FMessageAddress* ExistingAddress = ChannelToAddress.Find(ChannelId))
			{
				// Shouldn't happen.
				UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Duplicate channel ID {ChannelId} ({ExistingAddress})",
					ChannelId.ToString(), ExistingAddress->ToString());

				AddressToChannel.Remove(*ExistingAddress);
				ChannelToAddress.Remove(ChannelId);
			}

			if (FGuid* ExistingChannel = AddressToChannel.Find(InContext->GetSender()))
			{
				// Shouldn't happen.
				UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Duplicate aux address {SenderAddress} ({ExistingChannel})",
					InContext->GetSender().ToString(), ExistingChannel->ToString());

				ChannelToAddress.Remove(*ExistingChannel);
				AddressToChannel.Remove(InContext->GetSender());
			}

			ChannelToAddress.Add(ChannelId, InContext->GetSender());
			AddressToChannel.Add(InContext->GetSender(), ChannelId);

			FLiveLinkHubAuxChannelAcceptMessage* AcceptMessage =
				FMessageEndpoint::MakeMessage<FLiveLinkHubAuxChannelAcceptMessage>();
			AcceptMessage->ChannelId = ChannelId;
			MessageEndpoint->Send(AcceptMessage, EMessageFlags::Reliable, {}, nullptr, { InContext->GetSender() },
				FTimespan::Zero(), FDateTime::MaxValue());
		}
	);
}


void FLiveLinkHubUnrealDeviceAuxManager::RegisterTakeRecorderDelegates()
{
	if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>(); ensure(TakeRecorder))
	{
		TakeRecorder->GetOnRecordingStartedEvent().AddRaw(this, &FLiveLinkHubUnrealDeviceAuxManager::OnRecordingStarted);
		TakeRecorder->GetOnRecordingStoppedEvent().AddRaw(this, &FLiveLinkHubUnrealDeviceAuxManager::OnRecordingStopped);
	}
}


bool FLiveLinkHubUnrealDeviceAuxManager::IsKnownSender(const FMessageAddress& InAddress) const
{
	if (AddressToChannel.Contains(InAddress))
	{
		return true;
	}

	UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Message from unknown sender {Sender} will be ignored", InAddress.ToString());
	return false;
}


void FLiveLinkHubUnrealDeviceAuxManager::HandleAuxClose(
	const FLiveLinkHubAuxChannelCloseMessage& InMessage,
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
)
{
	UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received channel close from {Sender}", InContext->GetSender().ToString());

	if (!IsKnownSender(InContext->GetSender()))
	{
		return;
	}

	if (FGuid* ExistingChannel = AddressToChannel.Find(InContext->GetSender()))
	{
		if (*ExistingChannel != InMessage.ChannelId)
		{
			UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Channel close has wrong ID {ChannelId} (expected {ExistingId})",
				InMessage.ChannelId.ToString(), ExistingChannel->ToString());
		}

		AddressToChannel.Remove(InContext->GetSender());
	}
	else
	{
		// Shouldn't happen.
		UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Channel with address {Sender} not found", InContext->GetSender().ToString());
	}

	if (FMessageAddress* ExistingAddress = ChannelToAddress.Find(InMessage.ChannelId))
	{
		if (*ExistingAddress != InContext->GetSender())
		{
			UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Channel close not from expected sender {ExistingAddress}",
				ExistingAddress->ToString());
		}

		ChannelToAddress.Remove(InMessage.ChannelId);
	}
	else
	{
		// Shouldn't happen.
		UE_LOGFMT(LogLiveLinkUnrealDevice, Warning, "Channel with ID {ChannelId} not found", InMessage.ChannelId.ToString());
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::HandleSetSlateName(
	const FLiveLinkTakeRecorderCmd_SetSlateName& InCmd,
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
)
{
	UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received SetSlateName from {Sender}", InContext->GetSender().ToString());

	if (!IsKnownSender(InContext->GetSender()))
	{
		return;
	}

	if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>(); ensure(TakeRecorder))
	{
		TakeRecorder->SetSlateName(InCmd.SlateName);
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::HandleSetTakeNumber(
	const FLiveLinkTakeRecorderCmd_SetTakeNumber& InCmd,
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
)
{
	UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received SetTakeNumber from {Sender}", InContext->GetSender().ToString());

	if (!IsKnownSender(InContext->GetSender()))
	{
		return;
	}

	if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>(); ensure(TakeRecorder))
	{
		TakeRecorder->SetTakeNumber(InCmd.TakeNumber);
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::HandleStartRecording(
	const FLiveLinkTakeRecorderCmd_StartRecording& InCmd,
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
)
{
	UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received StartRecording from {Sender}", InContext->GetSender().ToString());

	if (!IsKnownSender(InContext->GetSender()))
	{
		return;
	}

	if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>(); ensure(TakeRecorder))
	{
		TakeRecorder->StartRecording();
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::HandleStopRecording(
	const FLiveLinkTakeRecorderCmd_StopRecording& InCmd,
	const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext
)
{
	UE_LOGFMT(LogLiveLinkUnrealDevice, Log, "Received StopRecording from {Sender}", InContext->GetSender().ToString());

	if (!IsKnownSender(InContext->GetSender()))
	{
		return;
	}

	if (UTakeRecorderSubsystem* TakeRecorder = GEngine->GetEngineSubsystem<UTakeRecorderSubsystem>(); ensure(TakeRecorder))
	{
		TakeRecorder->StopRecording();
	}
}


void FLiveLinkHubUnrealDeviceAuxManager::OnRecordingStarted(UTakeRecorder* InRecorder)
{
	TArray<FMessageAddress> ChannelAddresses;
	ChannelToAddress.GenerateValueArray(ChannelAddresses);

	FLiveLinkTakeRecorderEvent_RecordingStarted* EventMessage = FMessageEndpoint::MakeMessage<FLiveLinkTakeRecorderEvent_RecordingStarted>();
	MessageEndpoint->Send(EventMessage, EMessageFlags::Reliable, {}, nullptr, ChannelAddresses, FTimespan::Zero(), FDateTime::MaxValue());
}


void FLiveLinkHubUnrealDeviceAuxManager::OnRecordingStopped(UTakeRecorder* InRecorder)
{
	TArray<FMessageAddress> ChannelAddresses;
	ChannelToAddress.GenerateValueArray(ChannelAddresses);

	FLiveLinkTakeRecorderEvent_RecordingStopped* EventMessage = FMessageEndpoint::MakeMessage<FLiveLinkTakeRecorderEvent_RecordingStopped>();
	MessageEndpoint->Send(EventMessage, EMessageFlags::Reliable, {}, nullptr, ChannelAddresses, FTimespan::Zero(), FDateTime::MaxValue());
}
