// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMessageContext.h"
#include "LiveLinkHubConnectionManager.h"
#include "ILiveLinkHubMessagingModule.h"
#include "LiveLinkHubMessageBusSource.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "Misc/EngineVersion.h"

#if WITH_EDITOR
#include "LevelEditor.h"
#include "UnrealEdMisc.h"
#endif

/** 
 * Utility class that acts as a bridge between a LiveLinkHub Provider and a UE Client (or LLH instance) while handling backwards compatibility with previous versions of LLH Provider. 
 * The control channel can be set in Global mode, where it acts as a 
 */
class FLiveLinkHubControlChannel : public TSharedFromThis<FLiveLinkHubControlChannel>
{
public:
	/** Defines the operational mode of control channel. */
	enum class EChannelMode : uint8
	{
		Global, // Channel is owned by LiveLinkHubMessagingModule and handles all control messages directly.
		Source // Channel is owned by a LiveLinkHubMessageBusSource to support the V1 Discovery Protocol.
	};

	/** What mode this channel is acting in. */
	EChannelMode ChannelMode;

public:
	FLiveLinkHubControlChannel(EChannelMode InChannelMode)
	: ChannelMode(InChannelMode)
	{
	}

	~FLiveLinkHubControlChannel()
	{
		Shutdown();
	}

	/** Initializes the endpoint (when in Global mode) and registers callbacks. */
	void Initialize(FGuid SourceId = FGuid())
	{
		if (bInitialized)
		{
			return;
		}

		if (ChannelMode == EChannelMode::Global)
		{
			FMessageEndpoint::Builder EndpointBuilder = FMessageEndpoint::Builder(TEXT("LiveLinkHubControlChannel"));
			InitializeMessageEndpoint(EndpointBuilder);
			Endpoint = EndpointBuilder;
			if (Endpoint)
			{
				// For backwards compatibility with < 5.7
				Endpoint->Subscribe<FLiveLinkHubDiscoveryMessage>();
			}
		}

#if WITH_EDITOR
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->OnMapChanged().AddRaw(this, &FLiveLinkHubControlChannel::OnMapChanged);
		}
#endif

		CachedSourceId = SourceId;

		bInitialized = true;
	}

	/** Overrides the endpoint, allows LLHMessageBusSource to pass messages using its own endpoint.  */
	void SetEndpoint(TSharedPtr<FMessageEndpoint> InEndpoint)
	{
		Endpoint = MoveTemp(InEndpoint);
	}

	/** Get the address of the MessageEndpoint held by this channel. */
	FMessageAddress GetAddress() const
	{
		FMessageAddress Address;
		if (Endpoint)
		{
			Address = Endpoint->GetAddress();
		}

		return Address;
	}

	/** Registers the necessary handlers with the endpoint builder. */
	void InitializeMessageEndpoint(FMessageEndpoint::Builder& Builder)
	{
		// We need to dispatch these calls on the game thread, but the LiveLinkMessageBusSource receives data on AnyThread, so we use this lambda to dispatch those correctly.
		auto GameThreadDispatch = [this] <typename MessageType> (const MessageType & Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>&Context) mutable
		{
			if (IsInGameThread())
			{
				Handle<MessageType>(Message, Context);
			}
			else
			{
				TWeakPtr<FLiveLinkHubControlChannel> Self = AsShared();
				ExecuteOnGameThread(UE_SOURCE_LOCATION, [Self, Message, Context]() mutable
				{
					if (TSharedPtr<FLiveLinkHubControlChannel> Receiver = Self.Pin())
					{
						Receiver->Handle<MessageType>(Message, Context);
					}
				});
			}
		};

		Builder.Handling<FLiveLinkHubDiscoveryMessage>(GameThreadDispatch)
			.Handling<FLiveLinkHubCustomTimeStepSettings>(GameThreadDispatch)
			.Handling<FLiveLinkHubTimecodeSettings>(GameThreadDispatch)
			.Handling<FLiveLinkHubDisconnectMessage>(GameThreadDispatch)
			.WithCatchall(
				[this, GameThreadDispatch]
				(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext) mutable
				{
					const UScriptStruct* MessageTypeInfo = InContext->GetMessageTypeInfo().Get();

					// Handle subclasses of aux request.
			        if (MessageTypeInfo->IsChildOf(FLiveLinkHubAuxChannelRequestMessage::StaticStruct()))
					{
				        GameThreadDispatch(*(const FLiveLinkHubAuxChannelRequestMessage*)InContext->GetMessage(), InContext);
					}
				})
			;
	}

	/** Informs a LLH instance that this endpoint exists in order to create an entry in their MessageBus AddressBook. */
	void SendBeacon(const FMessageAddress& RemoteAddress) const
	{
		FLiveLinkHubMessagingModule& Module = FModuleManager::Get().GetModuleChecked<FLiveLinkHubMessagingModule>("LiveLinkHubMessaging");
		ELiveLinkTopologyMode Mode = Module.GetHostTopologyMode();

		FString CurrentLevelName;
		if (GWorld)
		{
			CurrentLevelName = GWorld->GetName();
		}

		Endpoint->Send(FMessageEndpoint::MakeMessage<FLiveLinkHubBeaconMessage>(Mode, Hostname, FApp::GetProjectName(), MoveTemp(CurrentLevelName)), EMessageFlags::Reliable, {}, nullptr, { RemoteAddress }, FTimespan::Zero(), FDateTime::MaxValue());
	}

	/** 
	 * Called by FLiveLinkHubMessageBusSource to ensure we disconnect the remote provider when that source is shutdown.
	 * @note We can't use the LiveLink OnSourceRemoved because the source ptr would be invalid by then.
	 */ 
	void OnSourceShutdown(const TSharedPtr<FLiveLinkHubMessageBusSource>& InSource)
	{
		if (!InSource)
		{
			return;
		}


		const FGuid SourceId = InSource->GetSourceId();
		if (!DisconnectingSources.Contains(SourceId))
		{
			UE_LOG(LogLiveLinkHubMessaging, Verbose, TEXT("MessageBusSource(%s) : Sending disconnect message to %s"), *InSource->GetAddress().ToString(), *InSource->GetConnectionAddress().ToString());

			FLiveLinkHubDisconnectMessage Message{ InSource->GetSourceType().ToString(), InSource->GetSourceMachineName().ToString(), SourceId };
			Endpoint->Send(FMessageEndpoint::MakeMessage<FLiveLinkHubDisconnectMessage>(MoveTemp(Message)), EMessageFlags::None, {}, nullptr, { InSource->GetConnectionAddress() }, FTimespan::Zero(), FDateTime::MaxValue());
			DisconnectingSources.Add(SourceId);
			TrackedProviders.Remove(InSource->GetConnectionAddress());
		}
	}

	/** Gather information about this client to put in a client info struct. */
	FLiveLinkClientInfoMessage CreateLiveLinkClientInfo() const
	{
		FLiveLinkClientInfoMessage ClientInfo;

		FString CurrentLevelName;
		if (GWorld && GWorld->GetCurrentLevel())
		{
			CurrentLevelName = GWorld->GetName();
		}

		// todo: Distinguish between UE and UEFN.
		ClientInfo.LongName = FString::Printf(TEXT("%s - %s %s"), TEXT("UE"), *FEngineVersion::Current().ToString(EVersionComponent::Patch), FPlatformProcess::ComputerName());
		ClientInfo.Status = ELiveLinkClientStatus::Connected;
		ClientInfo.Hostname = Hostname;
		ClientInfo.ProjectName = FApp::GetProjectName();
		ClientInfo.CurrentLevel = CurrentLevelName;


		FLiveLinkHubMessagingModule& Module = FModuleManager::Get().GetModuleChecked<FLiveLinkHubMessagingModule>("LiveLinkHubMessaging");
		ELiveLinkTopologyMode Mode = Module.GetHostTopologyMode();

		// Only populate this field if this is a Hub.
		if (Mode == ELiveLinkTopologyMode::Hub)
		{
			ClientInfo.LiveLinkInstanceName = Module.GetInstanceId().ToString();
			ClientInfo.TopologyMode = Mode;
		}

		ClientInfo.LiveLinkVersion = ILiveLinkClient::LIVELINK_VERSION;

		return ClientInfo;
	}

	/** Returning false from this delegate will respond to the sender with FLiveLinkHubAuxChannelRejectMessage. */
	using FAuxRequestDelegate = TTSDelegate<bool(const FLiveLinkHubAuxChannelRequestMessage& InMessage, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext)>;
	FAuxRequestDelegate OnAuxRequest;

private:
	/** Unregister callbacks. */
	void Shutdown()
	{
#if WITH_EDITOR
		if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
		{
			LevelEditorModule->OnMapChanged().RemoveAll(this);
		}
#endif
	}

	/** Templated method to dispatch messages using template specialization. */
	template <typename MessageType>
	void Handle(const MessageType& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		checkf(false, TEXT("Handler not implemented for %s."), *MessageType::StaticClass()->GetName());
	}

	/** Handle message telling this source to disconnect. */
	template <>
	void Handle(const FLiveLinkHubDisconnectMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		FGuid SourceId = Message.SourceGuid;
		if (!SourceId.IsValid())
		{
			// <5.7, Disconnect Message didn't specify source.
			SourceId = CachedSourceId;
		}

		DisconnectingSources.Add(SourceId);

		ILiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		Client->RemoveSource(SourceId);
		TrackedProviders.Remove(Context->GetSender());
	}

	/** Handle a custom time step settings message and update the engine's custom time step settings accordingly. */
	template <>
	void Handle(const FLiveLinkHubCustomTimeStepSettings& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		Message.AssignCustomTimeStepToEngine();
	}

	/** Handle a timecode settings message and update the engine's timecode settings accordingly. */
	template <>
	void Handle(const FLiveLinkHubTimecodeSettings& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		Message.AssignTimecodeSettingsAsProviderToEngine();
	}

	/** Handle a FLiveLinkHubDiscoveryMessage settings message and update the engine's timecode settings accordingly. */
	template <>
	void Handle(const FLiveLinkHubDiscoveryMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		if (ChannelMode != EChannelMode::Global)
		{
			// This message should only be handled by the global (LLHMessagingModule) control channel.
			return;
		}

		if (TrackedProviders.Contains(Context->GetSender()))
		{
			UE_LOG(LogLiveLinkHubMessaging, Display, TEXT("Ignoring discovery request from %s because we're already tracking it."), *Context->GetSender().ToString());
			return;
		}

		// Before this annotation was added, LiveLinkHub would automatically be added, so we need to keep the previous behavior if we discovered an older LiveLinkHub instance.
		ELiveLinkHubAutoConnectMode AutoConnectMode = ELiveLinkHubAutoConnectMode::Disabled;

		if (const FString* AutoConnectModeAnnotation = Context->GetAnnotations().Find(FLiveLinkHubMessageAnnotation::AutoConnectModeAnnotation))
		{
			int64 AutoConnectModeValue = StaticEnum<ELiveLinkHubAutoConnectMode>()->GetValueByName(**AutoConnectModeAnnotation);
			if (AutoConnectModeValue != INDEX_NONE)
			{
				AutoConnectMode = (ELiveLinkHubAutoConnectMode)AutoConnectModeValue;
			}
		}

		FLiveLinkHubMessagingModule& Module = FModuleManager::Get().GetModuleChecked<FLiveLinkHubMessagingModule>("LiveLinkHubMessaging");
		ELiveLinkTopologyMode Mode = Module.GetHostTopologyMode();

		bool bShouldConnect = GetDefault<ULiveLinkHubMessagingSettings>()->CanReceiveFrom(Mode, Message.Mode)
			&& LiveLinkHubConnectionManager::CanConnectTo(Message.MachineName, *Context, Module.GetInstanceId());
		if (bShouldConnect)
		{
			const double MachineTimeOffset = LiveLinkMessageBusHelper::CalculateProviderMachineOffset(Message.CreationTime, Context);

			// Only create a sub control channel for Sources talking to a LLH instance that's built before 5.7.
			TSharedPtr<FLiveLinkHubMessageBusSource> LiveLinkSource = MakeShared<FLiveLinkHubMessageBusSource>(FText::FromString(Message.ProviderName), FText::FromString(Message.MachineName), Context->GetSender(), MachineTimeOffset, Message.DiscoveryProtocolVersion);

			ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
			FGuid SourceId = Client.AddSource(LiveLinkSource);

			Module.OnConnectionEstablished().Broadcast(SourceId);
			TrackedProviders.Add(Context->GetSender());
		}
	}

	/** Handle aux channel requests by delegating to the provided callback. */
	template<>
	void Handle(const FLiveLinkHubAuxChannelRequestMessage& InMessage, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext)
	{
		if (OnAuxRequest.IsBound())
		{
			const bool bHandled = OnAuxRequest.Execute(InMessage, InContext);
			if (bHandled)
			{
				return;
			}
		}

		// Request not handled; respond with reject.
		FLiveLinkHubAuxChannelRejectMessage* RejectMessage = FMessageEndpoint::MakeMessage<FLiveLinkHubAuxChannelRejectMessage>();
		RejectMessage->ChannelId = InMessage.ChannelId;
		Endpoint->Send(RejectMessage, EMessageFlags::Reliable, nullptr, { InContext->GetSender() }, FTimespan::Zero(), FDateTime::MaxValue());
	}

#if WITH_EDITOR
	/** Handler called on map changed to update the livelink hub. */
	void OnMapChanged(UWorld* World, EMapChangeType ChangeType)
	{
		Endpoint->Send(FMessageEndpoint::MakeMessage<FLiveLinkClientInfoMessage>(CreateLiveLinkClientInfo()), EMessageFlags::None, {}, nullptr, TrackedProviders, FTimespan::Zero(), FDateTime::MaxValue());
	}
#endif

private:
	/** MessageBus endpoint used to transmit control messages. */
	TSharedPtr<FMessageEndpoint> Endpoint;

	/** Track sources in the process of disconnecting. */
	TSet<FGuid> DisconnectingSources;

	/** Name of this machine. */
	FString Hostname = FPlatformProcess::ComputerName();

	/** List of providers we've discovered. */
	TArray<FMessageAddress> TrackedProviders;

	/** When acting in Source mode, this is the ID of the source that owns this channel. */
	FGuid CachedSourceId;

	/** Flag to prevent initializing channel that's already initialized. */
	bool bInitialized = false;
};
