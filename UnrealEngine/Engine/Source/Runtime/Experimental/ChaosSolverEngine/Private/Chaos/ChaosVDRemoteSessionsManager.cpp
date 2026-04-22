// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosVDRemoteSessionsManager.h"

#include "ChaosVDRuntimeModule.h"
#include "IMessageBus.h"
#include "IMessagingModule.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "ChaosVisualDebugger/ChaosVDOptionalDataChannel.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "Misc/App.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosVDRemoteSessionsManager)

const FGuid FChaosVDRemoteSessionsManager::AllRemoteSessionsWrapperGUID = FGuid::NewGuid();
const FGuid FChaosVDRemoteSessionsManager::AllRemoteServersWrapperGUID = FGuid::NewGuid();
const FGuid FChaosVDRemoteSessionsManager::AllRemoteClientsWrapperGUID = FGuid::NewGuid();
const FGuid FChaosVDRemoteSessionsManager::AllSessionsWrapperGUID = FGuid::NewGuid();
const FGuid FChaosVDRemoteSessionsManager::CustomSessionsWrapperGUID = FGuid::NewGuid();
const FGuid FChaosVDRemoteSessionsManager::InvalidSessionGUID = FGuid();
const FGuid FChaosVDRemoteSessionsManager::LocalSessionID = FApp::GetInstanceId();

const FString FChaosVDRemoteSessionsManager::LocalEditorSessionName = TEXT("Local Editor");
const FGuid FChaosVDRemoteSessionsManager::LocalEditorSessionID = IsController() ? FApp::GetInstanceId() : InvalidSessionGUID;
const FName FChaosVDRemoteSessionsManager::MessageBusEndPointName = FName("CVDSessionManagerEndPoint");
const FString FChaosVDRemoteSessionsManager::AllRemoteSessionsTargetName = TEXT("All Remote");
const FString FChaosVDRemoteSessionsManager::AllRemoteServersTargetName = TEXT("All Remote Servers");
const FString FChaosVDRemoteSessionsManager::AllRemoteClientsTargetName = TEXT("All Remote Clients");
const FString FChaosVDRemoteSessionsManager::AllSessionsTargetName = TEXT("All Sessions");
const FString FChaosVDRemoteSessionsManager::CustomSessionsTargetName = TEXT("Custom Selection");

DEFINE_LOG_CATEGORY(LogChaosVDRemoteSession)

const FChaosVDTraceDetails& FChaosVDSessionInfo::GetConnectionDetails()
{
	return LastKnownConnectionDetails;
}

bool FChaosVDSessionInfo::IsRecording() const
{
	return LastKnownRecordingState.bIsRecording;
}

EChaosVDRecordingMode FChaosVDSessionInfo::GetRecordingMode() const
{
	return LastKnownConnectionDetails.IsValid() ? LastKnownConnectionDetails.Mode : LastRequestedRecordingMode;
}

EChaosVDRecordingMode FChaosVDSessionInfo::GetLastRequestedRecordingMode() const
{
	return LastRequestedRecordingMode;
}

void FChaosVDSessionInfo::SetLastRequestedRecordingMode(EChaosVDRecordingMode NewRecordingMode)
{
	LastRequestedRecordingMode = NewRecordingMode;
}

bool FChaosVDSessionInfo::IsConnected() const
{
	return false;
}

bool FChaosVDMultiSessionInfo::IsRecording() const
{
	bool bIsRecording = false;
	EnumerateInnerSessions([&bIsRecording](const TSharedRef<FChaosVDSessionInfo>& InSessionRef)
	{
		if (InSessionRef->IsRecording())
		{
			bIsRecording = true;
			return false;
		}
		return true;
	});

	return bIsRecording;
}

EChaosVDRecordingMode FChaosVDMultiSessionInfo::GetRecordingMode() const
{
	EChaosVDRecordingMode FirstValidInstanceRecordingMode = EChaosVDRecordingMode::Invalid;
	EnumerateInnerSessions([&FirstValidInstanceRecordingMode](const TSharedRef<FChaosVDSessionInfo>& InSessionRef)
	{
		EChaosVDRecordingMode RecordingMode = InSessionRef->GetRecordingMode();
		if (RecordingMode == EChaosVDRecordingMode::Invalid)
		{
			// In multi-session, all recordings will have the same recording mode, but not of them might report connected state at the same time
			// Therefore we need to continue searching until one of the session has a valid state before giving up.
			return true;
		}

		FirstValidInstanceRecordingMode = RecordingMode;
		return false;
	});

	return FirstValidInstanceRecordingMode;
}

void FChaosVDRemoteSessionsManager::RegisterBuiltInMessageTypes()
{
	SupportedMessageTypes.Emplace(FChaosVDSessionPong::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDRecordingStatusMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDSessionPing::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDStartRecordingCommandMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDStopRecordingCommandMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDChannelStateChangeCommandMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDFullSessionInfoRequestMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDFullSessionInfoResponseMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDChannelStateChangeResponseMessage::StaticStruct());
	SupportedMessageTypes.Emplace(FChaosVDTraceConnectionDetailsMessage::StaticStruct());
}

FChaosVDRemoteSessionsManager::FChaosVDRemoteSessionsManager()
{
	RegisterBuiltInMessageTypes();
}

void FChaosVDRemoteSessionsManager::Initialize(const TSharedPtr<IMessageBus>& InMessageBus)
{
	ensureMsgf(!InMessageBus, TEXT("Initialize(MessageBusPtr) is deprecated!. The provided message buss will be ignored"));

	Initialize();
}

constexpr bool FChaosVDRemoteSessionsManager::IsController()
{
#if WITH_EDITOR
	return true;
#else
	return false;
#endif
}

TWeakPtr<FChaosVDSessionInfo> FChaosVDRemoteSessionsManager::GetSessionInfo(FGuid Id)
{
	if (TSharedPtr<FChaosVDSessionInfo>* FoundSessionPtrPtr = ActiveSessionsByInstanceId.Find(Id))
	{
		return *FoundSessionPtrPtr;
	}

	return nullptr;
}

TSharedPtr<FChaosVDSessionInfo> FChaosVDRemoteSessionsManager::CreatedWrapperSessionInfo(FGuid InstanceId, const FString& SessionName)
{
	TSharedPtr<FChaosVDMultiSessionInfo> NewSessionInfo = MakeShared<FChaosVDMultiSessionInfo>();
	
	NewSessionInfo->InstanceId = InstanceId;
	NewSessionInfo->SessionName = SessionName;

	return NewSessionInfo;
}

TSharedPtr<FMessageEndpoint> FChaosVDRemoteSessionsManager::CreateEndPoint(const TSharedRef<IMessageBus>& InMessageBus)
{
	FMessageEndpointBuilder EndPointBuilder = FMessageEndpoint::Builder(MessageBusEndPointName, InMessageBus)
	                                          .Handling<FChaosVDSessionPing>(this, &FChaosVDRemoteSessionsManager::HandleSessionPingMessage)
	                                          .Handling<FChaosVDStartRecordingCommandMessage>(this, &FChaosVDRemoteSessionsManager::HandleRecordingStartCommandMessage)
	                                          .Handling<FChaosVDStopRecordingCommandMessage>(this, &FChaosVDRemoteSessionsManager::HandleRecordingStopCommandMessage)
	                                          .Handling<FChaosVDChannelStateChangeCommandMessage>(this, &FChaosVDRemoteSessionsManager::HandleChangeDataChannelStateCommandMessage)
	                                          .Handling<FChaosVDFullSessionInfoRequestMessage>(this, &FChaosVDRemoteSessionsManager::HandleFullSessionStateRequestMessage);

	if (IsController())
	{
		EndPointBuilder.Handling<FChaosVDSessionPong>(this, &FChaosVDRemoteSessionsManager::HandleSessionPongMessage)
						.Handling<FChaosVDRecordingStatusMessage>(this, &FChaosVDRemoteSessionsManager::HandleRecordingStatusUpdateMessage)
						.Handling<FChaosVDFullSessionInfoResponseMessage>(this, &FChaosVDRemoteSessionsManager::HandleFullSessionStateResponseMessage)
						.Handling<FChaosVDChannelStateChangeResponseMessage>(this, &FChaosVDRemoteSessionsManager::HandleChangeDataChannelStateResponseMessage)
						.Handling<FChaosVDTraceConnectionDetailsMessage>(this, &FChaosVDRemoteSessionsManager::HandleConnectionDetailsUpdateMessage);
	}

	return EndPointBuilder.Build();
}

void FChaosVDRemoteSessionsManager::ReInitializeMessagingSystem(const TSharedPtr<IMessageBus>& InMessageBus)
{
	ShutdownMessagingSystem();
	InitializeMessagingSystem(InMessageBus);
}

void FChaosVDRemoteSessionsManager::RegisterExternalSupportedMessageType(const UScriptStruct* ScriptStruct)
{
	if (ensure(ScriptStruct))
	{
		SupportedMessageTypes.Emplace(ScriptStruct);
	}
}

void FChaosVDRemoteSessionsManager::InitializeMessagingSystem(const TSharedPtr<IMessageBus>& InMessageBus)
{
	if (!ensure(InMessageBus))
	{
		return;
	}

	MessageBusPtr = InMessageBus;
	MessageEndpoint = CreateEndPoint(InMessageBus.ToSharedRef());
	
	if (!ensure(MessageEndpoint))
	{
		return;
	}

	if (IsController())
	{
		MessageEndpoint->Subscribe<FChaosVDSessionPong>();
		MessageEndpoint->Subscribe<FChaosVDRecordingStatusMessage>();
		MessageEndpoint->Subscribe<FChaosVDFullSessionInfoResponseMessage>();
		MessageEndpoint->Subscribe<FChaosVDChannelStateChangeResponseMessage>();
		MessageEndpoint->Subscribe<FChaosVDTraceConnectionDetailsMessage>();
	}

	MessageEndpoint->Subscribe<FChaosVDSessionPing>();
	MessageEndpoint->Subscribe<FChaosVDStartRecordingCommandMessage>();
	MessageEndpoint->Subscribe<FChaosVDStopRecordingCommandMessage>();
	MessageEndpoint->Subscribe<FChaosVDChannelStateChangeCommandMessage>();
	MessageEndpoint->Subscribe<FChaosVDFullSessionInfoRequestMessage>();

	if (bInitialized)
	{
		MessageEndpoint->Enable();
	}
	else
	{
		MessageEndpoint->Disable();
	}

	MessagingInitializedDelegate.Broadcast(InMessageBus, MessageEndpoint);
}

void FChaosVDRemoteSessionsManager::ShutdownMessagingSystem()
{
	if (MessageEndpoint)
	{
		MessageEndpoint->Unsubscribe<FChaosVDSessionPong>();
		MessageEndpoint->Unsubscribe<FChaosVDRecordingStatusMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDSessionPing>();
		MessageEndpoint->Unsubscribe<FChaosVDStartRecordingCommandMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDStopRecordingCommandMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDChannelStateChangeCommandMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDFullSessionInfoRequestMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDFullSessionInfoResponseMessage>();
		MessageEndpoint->Unsubscribe<FChaosVDChannelStateChangeResponseMessage>();
	}

	MessageBusPtr.Reset();
	MessageEndpoint = nullptr;
}

void FChaosVDRemoteSessionsManager::Initialize()
{
#if !defined(WITH_CHAOS_VISUAL_DEBUGGER_EXTERNAL_MESSAGING) || !WITH_CHAOS_VISUAL_DEBUGGER_EXTERNAL_MESSAGING
	InitializeMessagingSystem(IMessagingModule::Get().GetDefaultBus());
#endif

	ActiveSessionsByInstanceId.Add(AllRemoteSessionsWrapperGUID, CreatedWrapperSessionInfo(AllRemoteSessionsWrapperGUID, AllRemoteSessionsTargetName));
	ActiveSessionsByInstanceId.Add(AllRemoteServersWrapperGUID, CreatedWrapperSessionInfo(AllRemoteServersWrapperGUID, AllRemoteServersTargetName));
	ActiveSessionsByInstanceId.Add(AllRemoteClientsWrapperGUID, CreatedWrapperSessionInfo(AllRemoteClientsWrapperGUID, AllRemoteClientsTargetName));
	ActiveSessionsByInstanceId.Add(AllSessionsWrapperGUID, CreatedWrapperSessionInfo(AllSessionsWrapperGUID, AllSessionsTargetName));
	ActiveSessionsByInstanceId.Add(CustomSessionsWrapperGUID, CreatedWrapperSessionInfo(CustomSessionsWrapperGUID, CustomSessionsTargetName));

	if (MessageEndpoint)
	{
		MessageEndpoint->Enable();
	}

	bInitialized = true;
}

void FChaosVDRemoteSessionsManager::Shutdown()
{
	bInitialized = false;
	ShutdownMessagingSystem();
}

void FChaosVDRemoteSessionsManager::EnumerateMessageTypes(const FVisitorFunction& InVisitor)
{
	for (const UScriptStruct* MessageType : SupportedMessageTypes)
	{
		if (MessageType)
		{
			InVisitor(MessageType);
		}
	}
}

void FChaosVDRemoteSessionsManager::StartSessionDiscovery()
{
	if (TickHandle.IsValid())
	{
		UE_LOG(LogChaosVDRemoteSession, Warning, TEXT("[%hs] Session discovery already started"), __func__);
		return;
	}
	
	constexpr float TickInterval = 1.0f;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FChaosVDRemoteSessionsManager::Tick), TickInterval);
}

void FChaosVDRemoteSessionsManager::StopSessionDiscovery()
{
	if (TickHandle.IsValid())
	{
		RemoveExpiredSessions(ERemoveSessionOptions::ForceRemoveAll);
		FTSTicker::RemoveTicker(TickHandle);
		TickHandle = FTSTicker::FDelegateHandle();
	}
}

void FChaosVDRemoteSessionsManager::PublishRecordingStatusUpdate(const FChaosVDRecordingStatusMessage& InUpdateMessage)
{
	if (ensure(MessageEndpoint))
	{
		MessageEndpoint->Publish(FMessageEndpoint::MakeMessage<FChaosVDRecordingStatusMessage>(InUpdateMessage), EMessageScope::Network);
	}
}

void FChaosVDRemoteSessionsManager::PublishTraceConnectionDetailsUpdate(const FChaosVDTraceConnectionDetailsMessage& InUpdateMessage)
{
	if (ensure(MessageEndpoint))
	{
		MessageEndpoint->Publish(FMessageEndpoint::MakeMessage<FChaosVDTraceConnectionDetailsMessage>(InUpdateMessage), EMessageScope::Network);
	}
}

void FChaosVDRemoteSessionsManager::PublishDataChannelStateChangeUpdate(const FChaosVDChannelStateChangeResponseMessage& InNewStateData)
{
	if (ensure(MessageEndpoint))
	{
		MessageEndpoint->Publish(FMessageEndpoint::MakeMessage<FChaosVDChannelStateChangeResponseMessage>(InNewStateData), EMessageScope::Network);
	}
}

void FChaosVDRemoteSessionsManager::SendStartRecordingCommand(const FMessageAddress& InDestinationAddress, const FChaosVDStartRecordingCommandMessage& RecordingStartCommandParams)
{
	if (!MessageEndpoint)
	{
		UE_LOG(LogChaosVDRemoteSession, Error, TEXT("[%hs] Failed to send command | Invalid endpoint."), __func__);
		return;
	}

	MessageEndpoint->Send(
	FMessageEndpoint::MakeMessage<FChaosVDStartRecordingCommandMessage>(RecordingStartCommandParams),
	FChaosVDStartRecordingCommandMessage::StaticStruct(),
	EMessageFlags::Reliable,
	nullptr,
	TArrayBuilder<FMessageAddress>().Add(InDestinationAddress),
	FTimespan::Zero(),
	FDateTime::MaxValue());
}

void FChaosVDRemoteSessionsManager::SendStopRecordingCommand(const FMessageAddress& InDestinationAddress)
{
	if (!MessageEndpoint)
	{
		UE_LOG(LogChaosVDRemoteSession, Error, TEXT("[%s] Failed to send command | Invalid endpoint."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	MessageEndpoint->Send(
	FMessageEndpoint::MakeMessage<FChaosVDStopRecordingCommandMessage>(),
	FChaosVDStopRecordingCommandMessage::StaticStruct(),
	EMessageFlags::Reliable,
	nullptr,
	TArrayBuilder<FMessageAddress>().Add(InDestinationAddress),
	FTimespan::Zero(),
	FDateTime::MaxValue());
}

void FChaosVDRemoteSessionsManager::SendDataChannelStateChangeCommand(const FMessageAddress& InDestinationAddress,const FChaosVDChannelStateChangeCommandMessage& InNewStateData)
{
	if (!MessageEndpoint)
	{
		UE_LOG(LogChaosVDRemoteSession, Error, TEXT("[%s] Failed to send command | Invalid endpoint."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	MessageEndpoint->Send(
	FMessageEndpoint::MakeMessage<FChaosVDChannelStateChangeCommandMessage>(InNewStateData),
	FChaosVDChannelStateChangeCommandMessage::StaticStruct(),
	EMessageFlags::Reliable,
	nullptr,
	TArrayBuilder<FMessageAddress>().Add(InDestinationAddress),
	FTimespan::Zero(),
	FDateTime::MaxValue());
}

void FChaosVDRemoteSessionsManager::SendFullSessionStateRequestCommand(const FMessageAddress& InDestinationAddress)
{
	if (!MessageEndpoint)
	{
		UE_LOG(LogChaosVDRemoteSession, Error, TEXT("[%s] Failed to send command | Invalid endpoint."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	MessageEndpoint->Send(
	FMessageEndpoint::MakeMessage<FChaosVDFullSessionInfoRequestMessage>(),
	FChaosVDFullSessionInfoRequestMessage::StaticStruct(),
	EMessageFlags::Reliable,
	nullptr,
	TArrayBuilder<FMessageAddress>().Add(InDestinationAddress),
	FTimespan::Zero(),
	FDateTime::MaxValue());
}

bool FChaosVDRemoteSessionsManager::Tick(float DeltaTime)
{
	if (IsController())
	{
		SendPing();
		RemoveExpiredSessions();
	}

	return true;
}

void FChaosVDRemoteSessionsManager::SendPing()
{
	if (ensure(MessageEndpoint))
	{
		if (FChaosVDSessionPing* SessionPingData = FMessageEndpoint::MakeMessage<FChaosVDSessionPing>())
		{
			SessionPingData->ControllerInstanceId = FApp::GetInstanceId();
			MessageEndpoint->Publish(SessionPingData, EMessageScope::Network);
		}
	}
}

void FChaosVDRemoteSessionsManager::SendPong(const FChaosVDSessionPing& InMessage)
{
	if (!ensure(MessageEndpoint))
	{
		return;
	}

	if (FChaosVDSessionPong* PongMessage = FMessageEndpoint::MakeMessage<FChaosVDSessionPong>())
	{
		PongMessage->InstanceId = FApp::GetInstanceId();
		PongMessage->SessionId = FApp::GetSessionId();
		PongMessage->BuildTargetType = static_cast<uint8>(FApp::GetBuildTargetType());

		if (InMessage.ControllerInstanceId == PongMessage->InstanceId)
		{
			PongMessage->SessionName = LocalEditorSessionName;
		}
		else
		{
			FString AppSessionName = FApp::GetSessionName();
			PongMessage->SessionName = AppSessionName == TEXT("None") || AppSessionName.IsEmpty() ? FString::Format(TEXT("{0} {1} {2}"), { FApp::GetProjectName(), FString(LexToString(FApp::GetBuildTargetType())), FString::FromInt(FPlatformProcess::GetCurrentProcessId()) }) : AppSessionName;	
		}

		MessageEndpoint->Publish(PongMessage, EMessageScope::Network);
	}
}

void FChaosVDRemoteSessionsManager::RegisterSessionInMultiSessionWrapper(const TSharedRef<FChaosVDSessionInfo>& InSessionInfoRef)
{
	if (InSessionInfoRef->SessionName != LocalEditorSessionName )
	{
		StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteSessionsWrapperGUID))->InnerSessionsByInstanceID.Emplace(InSessionInfoRef->InstanceId, InSessionInfoRef);

		if (InSessionInfoRef->BuildTargetType == EBuildTargetType::Server)
		{
			StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteServersWrapperGUID))->InnerSessionsByInstanceID.Emplace(InSessionInfoRef->InstanceId, InSessionInfoRef);
		}
		else
		{
			StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteClientsWrapperGUID))->InnerSessionsByInstanceID.Emplace(InSessionInfoRef->InstanceId, InSessionInfoRef);
		}
	}

	StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllSessionsWrapperGUID))->InnerSessionsByInstanceID.Emplace(InSessionInfoRef->InstanceId, InSessionInfoRef);
}

void FChaosVDRemoteSessionsManager::DeRegisterSessionInMultiSessionWrapper(const TSharedRef<FChaosVDSessionInfo>& InSessionInfoRef)
{
	StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteSessionsWrapperGUID))->InnerSessionsByInstanceID.Remove(InSessionInfoRef->InstanceId);
	StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllSessionsWrapperGUID))->InnerSessionsByInstanceID.Remove(InSessionInfoRef->InstanceId);
	StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteServersWrapperGUID))->InnerSessionsByInstanceID.Remove(InSessionInfoRef->InstanceId);
	StaticCastSharedPtr<FChaosVDMultiSessionInfo>(ActiveSessionsByInstanceId.FindChecked(AllRemoteClientsWrapperGUID))->InnerSessionsByInstanceID.Remove(InSessionInfoRef->InstanceId);
}

void FChaosVDRemoteSessionsManager::ProcessPendingMessagesForSession(const FChaosVDSessionPong& InMessage, const TSharedRef<FChaosVDSessionInfo>& InSessionInfoPtr)
{
	PendingRecordingStatusMessages.RemoveAndCopyValue(InMessage.InstanceId, InSessionInfoPtr->LastKnownRecordingState);
	PendingRecordingConnectionDetailsMessages.RemoveAndCopyValue(InMessage.InstanceId, InSessionInfoPtr->LastKnownConnectionDetails);
}

void FChaosVDRemoteSessionsManager::HandleSessionPongMessage(const FChaosVDSessionPong& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = ActiveSessionsByInstanceId.FindOrAdd(InMessage.InstanceId);

	if (!SessionInfoPtr)
	{
		SessionInfoPtr = MakeShared<FChaosVDSessionInfo>();
		SessionInfoPtr->Address = InContext->GetSender();
		SessionInfoPtr->InstanceId = InMessage.InstanceId;
		SessionInfoPtr->SessionName = InMessage.SessionName;
		SessionInfoPtr->BuildTargetType = static_cast<EBuildTargetType>(InMessage.BuildTargetType);

		RegisterSessionInMultiSessionWrapper(SessionInfoPtr.ToSharedRef());

		SessionDiscoveredDelegate.Broadcast(SessionInfoPtr->InstanceId);

		// This is the first time we see this session, so we need to request the rest of its state so we can properly populate the UI
		SendFullSessionStateRequestCommand(SessionInfoPtr->Address);
	}

	SessionInfoPtr->LastPingTime = FDateTime::UtcNow();

	ProcessPendingMessagesForSession(InMessage, SessionInfoPtr.ToSharedRef());
	
	SessionsUpdatedDelegate.Broadcast();
}

void FChaosVDRemoteSessionsManager::HandleSessionPingMessage(const FChaosVDSessionPing& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	// If this instance is not running trace, don't answer to the ping as the CVD instance will not be able to do anything useful with it
#if	!CHAOS_VISUAL_DEBUGGER_WITHOUT_TRACE
	SendPong(InMessage);
#endif
}

void FChaosVDRemoteSessionsManager::HandleRecordingStatusUpdateMessage(const FChaosVDRecordingStatusMessage& Message, const TSharedRef<IMessageContext>& InContext)
{
	if (TSharedPtr<FChaosVDSessionInfo>* SessionInfoPtrPtr = ActiveSessionsByInstanceId.Find(Message.InstanceId))
	{
		TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = *SessionInfoPtrPtr;
		check(SessionInfoPtr);

		if (SessionInfoPtr->LastKnownRecordingState.bIsRecording != Message.bIsRecording)
		{
			if (Message.bIsRecording)
			{
				RecordingStartedDelegate.Broadcast(SessionInfoPtr);
			}
			else
			{
				RecordingStoppedDelegate.Broadcast(SessionInfoPtr);
			}
		}

		SessionInfoPtr->LastKnownRecordingState = Message;
	}
	else
	{
		PendingRecordingStatusMessages.FindOrAdd(Message.InstanceId) = Message;
	}
}

void FChaosVDRemoteSessionsManager::HandleConnectionDetailsUpdateMessage(const FChaosVDTraceConnectionDetailsMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	if (TSharedPtr<FChaosVDSessionInfo>* SessionInfoPtrPtr = ActiveSessionsByInstanceId.Find(InMessage.InstanceId))
	{
		TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = *SessionInfoPtrPtr;
		check(SessionInfoPtr);

		SessionInfoPtr->LastKnownConnectionDetails = InMessage.TraceDetails;
	}
	else
	{
		PendingRecordingConnectionDetailsMessages.FindOrAdd(InMessage.InstanceId) = InMessage.TraceDetails;
	}
}

void FChaosVDRemoteSessionsManager::HandleRecordingStartCommandMessage(const FChaosVDStartRecordingCommandMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
#if WITH_CHAOS_VISUAL_DEBUGGER
	UE_AUTORTFM_ONCOMMIT(InMessage, InContext)
	{
		FChaosVisualDebuggerTrace::OverrideDefaultEnabledDataChannels(InMessage.DataChannelsEnabledOverrideList);
		FChaosVDRuntimeModule::Get().StartRecording(InMessage);
	};

#endif
}

void FChaosVDRemoteSessionsManager::HandleRecordingStopCommandMessage(const FChaosVDStopRecordingCommandMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
#if WITH_CHAOS_VISUAL_DEBUGGER
	UE_AUTORTFM_ONCOMMIT()
	{
		FChaosVDRuntimeModule::Get().StopRecording();
	};
#endif
}

void FChaosVDRemoteSessionsManager::HandleChangeDataChannelStateCommandMessage(const FChaosVDChannelStateChangeCommandMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
#if WITH_CHAOS_VISUAL_DEBUGGER
	UE_AUTORTFM_ONCOMMIT(InMessage)
	{
		using namespace Chaos::VisualDebugger;
		if (TSharedPtr<FChaosVDOptionalDataChannel> ChannelInstance = FChaosVDDataChannelsManager::Get().GetChannelById(FName(InMessage.NewState.ChannelName)))
		{
			ChannelInstance->SetChannelEnabled(InMessage.NewState.bIsEnabled);
		}
	};
#endif
}

void FChaosVDRemoteSessionsManager::HandleChangeDataChannelStateResponseMessage(const FChaosVDChannelStateChangeResponseMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	if (TSharedPtr<FChaosVDSessionInfo>* SessionInfoPtrPtr = ActiveSessionsByInstanceId.Find(InMessage.InstanceID))
	{
		if (TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = *SessionInfoPtrPtr)
		{
			if (FChaosVDDataChannelState* FoundChannelState = SessionInfoPtr->DataChannelsStatesByName.Find(InMessage.NewState.ChannelName))
			{
				*FoundChannelState = InMessage.NewState;
			}
		}
	}
}

void FChaosVDRemoteSessionsManager::HandleFullSessionStateRequestMessage(const FChaosVDFullSessionInfoRequestMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	if (!ensure(MessageEndpoint))
	{
		return;
	}
	
	if (FChaosVDFullSessionInfoResponseMessage* FullSessionStateResponse = FMessageEndpoint::MakeMessage<FChaosVDFullSessionInfoResponseMessage>())
	{
		FullSessionStateResponse->InstanceId = FApp::GetInstanceId();

#if WITH_CHAOS_VISUAL_DEBUGGER
		FullSessionStateResponse->bIsRecording = FChaosVDRuntimeModule::Get().IsRecording();

		using namespace Chaos::VisualDebugger;
		FChaosVDDataChannelsManager::Get().EnumerateChannels([&FullSessionStateResponse](const TSharedRef<FChaosVDOptionalDataChannel>& Channel)
		{
			FChaosVDChannelStateChangeCommandMessage DataChannelStateMessage = {Channel->GetId().ToString(), Channel->IsChannelEnabled(), Channel->CanChangeEnabledState() };
			FullSessionStateResponse->DataChannelsStates.Emplace(DataChannelStateMessage.NewState);
			return true;
		});
#endif

		MessageEndpoint->Send(
			FullSessionStateResponse,
			FChaosVDFullSessionInfoResponseMessage::StaticStruct(),
			EMessageFlags::Reliable,
			nullptr,
			TArrayBuilder<FMessageAddress>().Add(InContext->GetSender()),
			FTimespan::Zero(),
			FDateTime::MaxValue());
	}
}

void FChaosVDRemoteSessionsManager::HandleFullSessionStateResponseMessage(const FChaosVDFullSessionInfoResponseMessage& InMessage, const TSharedRef<IMessageContext>& InContext)
{
	if (TSharedPtr<FChaosVDSessionInfo>* SessionInfoPtrPtr = ActiveSessionsByInstanceId.Find(InMessage.InstanceId))
	{
		if (TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = *SessionInfoPtrPtr)
		{
			SessionInfoPtr->LastKnownRecordingState.bIsRecording = InMessage.bIsRecording;

			for (const FChaosVDDataChannelState& ChannelState : InMessage.DataChannelsStates)
			{
				SessionInfoPtr->DataChannelsStatesByName.Emplace(ChannelState.ChannelName, ChannelState);
			}
		}
	}
}

void FChaosVDRemoteSessionsManager::RemoveExpiredSessions(ERemoveSessionOptions Options)
{
	bool bAnySessionRemoved = false;
	FDateTime CurrentTime = FDateTime::UtcNow();
	for (TMap<FGuid, TSharedPtr<FChaosVDSessionInfo>>::TIterator RemoveIterator = ActiveSessionsByInstanceId.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		TSharedPtr<FChaosVDSessionInfo>& SessionInfoPtr = RemoveIterator.Value();
		if (!SessionInfoPtr)
		{
			RemoveIterator.RemoveCurrent();
			bAnySessionRemoved = true;
			continue;
		}

		if (!EnumHasAnyFlags(SessionInfoPtr->GetSessionTypeAttributes(), EChaosVDRemoteSessionAttributes::CanExpire))
		{
			continue;
		}

		FTimespan ElapsedTime = CurrentTime - SessionInfoPtr->LastPingTime;

		// A session goes into busy state if we are attempting to issue a command that might stall the target, currently that only happens on recording start commands of complex maps
		// In these cases, we need to allow more time between pings. If a recording command failed, it is expected the state to be changed to Ready again
		const float MaxAllowedTimeBetweenPings = SessionInfoPtr->ReadyState == EChaosVDRemoteSessionReadyState::Busy || SessionInfoPtr->IsRecording() ? 60.0f : 3.0f;
		if (EnumHasAnyFlags(Options, ERemoveSessionOptions::ForceRemoveAll) || ElapsedTime > FTimespan::FromSeconds(MaxAllowedTimeBetweenPings))
		{
			SessionExpiredDelegate.Broadcast(SessionInfoPtr->InstanceId);

			DeRegisterSessionInMultiSessionWrapper(SessionInfoPtr.ToSharedRef());
			RemoveIterator.RemoveCurrent();
			bAnySessionRemoved = true;
		}
	}

	if (bAnySessionRemoved)
	{
		SessionsUpdatedDelegate.Broadcast();
	}
}
