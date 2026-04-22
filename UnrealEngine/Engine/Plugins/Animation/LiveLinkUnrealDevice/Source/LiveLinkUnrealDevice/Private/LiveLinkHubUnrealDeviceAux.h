// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMessageContext.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"


struct FLiveLinkHubAuxChannelCloseMessage;
struct FLiveLinkTakeRecorderCmd_SetSlateName;
struct FLiveLinkTakeRecorderCmd_SetTakeNumber;
struct FLiveLinkTakeRecorderCmd_StartRecording;
struct FLiveLinkTakeRecorderCmd_StopRecording;
class FMessageEndpoint;
class IMessageContext;
class UTakeRecorder;


/**
 * Configures a Live Link Hub auxiliary message channel for communicating with Unreal devices.
 * 
 * Handles incoming Take Recorder commands, and relays events to connected devices.
 */
class FLiveLinkHubUnrealDeviceAuxManager
{
public:
	FLiveLinkHubUnrealDeviceAuxManager();
	~FLiveLinkHubUnrealDeviceAuxManager();

private:
	// Init helpers
	void RegisterRequestHandler();
	void RegisterTakeRecorderDelegates();

	// Take Recorder event handlers
	void OnRecordingStarted(UTakeRecorder* InRecorder);
	void OnRecordingStopped(UTakeRecorder* InRecorder);

	// Message handlers
	bool IsKnownSender(const FMessageAddress& InAddress) const;

	void HandleAuxClose(const FLiveLinkHubAuxChannelCloseMessage& InMessage, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext);

	void HandleSetSlateName(const FLiveLinkTakeRecorderCmd_SetSlateName& InCmd, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext);
	void HandleSetTakeNumber(const FLiveLinkTakeRecorderCmd_SetTakeNumber& InCmd, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext);
	void HandleStartRecording(const FLiveLinkTakeRecorderCmd_StartRecording& InCmd, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext);
	void HandleStopRecording(const FLiveLinkTakeRecorderCmd_StopRecording& InCmd, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& InContext);

private:
	TSharedPtr<FMessageEndpoint> MessageEndpoint;

	TMap<FGuid, FMessageAddress> ChannelToAddress;
	TMap<FMessageAddress, FGuid> AddressToChannel;
};
