// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenlockedCustomTimeStep.h"
#include "ILiveLinkHubMessagingModule.h"
#include "ILiveLinkClient.h"
#include "LiveLinkMessages.h"
#include "LiveLinkTimecodeProvider.h"
#include "LiveLinkTypes.h"
#include "Misc/App.h"
#include "Misc/FrameRate.h"

#include "LiveLinkCustomTimeStep.h"

#include "LiveLinkHubMessages.generated.h"

#define UE_API LIVELINKHUBMESSAGING_API

/** Whether if and how LiveLinkHub should auto-connect to UE clients on the network. */
UENUM()
enum class ELiveLinkHubAutoConnectMode : uint8
{
	Disabled   UMETA(ToolTip = "Don't add any clients automatically."),
	All 	   UMETA(ToolTip = "Add any client that was found on the network, unless it doesn't match one of the filters."),
	LocalOnly  UMETA(ToolTip = "Add any client running on this machine, overriding any current filter.")
};

/** 
 * List of LiveLinkHub annotations.
 */
struct FLiveLinkHubMessageAnnotation
{
	/**
	 * Annotation put on MessageBus messages to indicate the type of provider used.
	 * Absence of provider type means that the message comes from a regular LiveLinkProvider.
	 */
	static UE_API FName ProviderTypeAnnotation;

	/** Annotation to indicate if this source should be automatically added to the list of LiveLink sources. */
	static UE_API FName AutoConnectModeAnnotation;

	/** Instance ID annotation used to identify the running LLH instance. */
	static UE_API FName IdAnnotation;
};

namespace UE::LiveLinkHub::Private
{
	/** LiveLink Hub provider type used to identify messages coming from a LiveLinkProvider that lives on a LiveLink Hub. */
	extern const LIVELINKHUBMESSAGING_API FName LiveLinkHubProviderType;
}

UCLASS(Hidden)
class ULiveLinkHubCustomTimeStep : public ULiveLinkCustomTimeStep
{
	GENERATED_BODY()
};

USTRUCT()
struct FLiveLinkHubCustomTimeStepSettings
{
	GENERATED_BODY()

	/** If this is true, the engine custom time step will be reset. */
	UPROPERTY()
	bool bResetCustomTimeStep = false;

	/** Corresponds to the lock step mode in ULiveLinkCustomTimeStep. */
	UPROPERTY(config, EditAnywhere, Category = "Frame Lock")
	bool bLockStepMode = true;

	/** Corresponds to the frame rate divider in ULiveLinkCustomTimeStep */
	UPROPERTY(config, EditAnywhere, Category = "Frame Lock", meta = (ClampMin = 1, ClampMax = 256, UIMin = 1, UIMax = 256))
	uint32 FrameRateDivider = 1;

	/** If we are locking the editor frame rate to the subject then this property holds that subject name. */
	UPROPERTY(config, EditAnywhere, Category = "Frame Lock")
	FLiveLinkSubjectName SubjectName;

	/** Desired frame rate to lock the editor. This corresponds to the LiveLinkDataRate in ULiveLinkCustomTimeStep */
	UPROPERTY(config, EditAnywhere, Category = "Frame Lock")
	FFrameRate CustomTimeStepRate = FFrameRate(60, 1);

	/** Assign the frame lock settings to the engine. */
	UE_API void AssignCustomTimeStepToEngine() const;
};

UENUM()
enum class ELiveLinkHubTimecodeSource
{
	// Not defined by the Hub and thus should use the default system settings.
	NotDefined,

	// Using system time of the editor.
	SystemTimeEditor,

	// Using the provided subject name
	UseSubjectName
};

/** Special message to communicate / override time code used by the connected editor. */
USTRUCT()
struct FLiveLinkHubTimecodeSettings
{
	GENERATED_BODY()

	/** Source time code value.  If it is not defined then we use the default time code provider in the engine. */
	UPROPERTY(config, EditAnywhere, Category = "Timecode")
	ELiveLinkHubTimecodeSource Source = ELiveLinkHubTimecodeSource::NotDefined;

	/** Name of the subject to map timecode if Source == ELiveLinkHubTimecodeSource::UseSubjectName */
	UPROPERTY(config, EditAnywhere, Category = "Timecode")
	FLiveLinkSubjectName SubjectName;

	/** Desired frame rate to set if Source == ELiveLinkHubTimecodeSource::SystemTimeEditor. */
	UPROPERTY(config, EditAnywhere, Category = "Timecode", meta = (EditCondition = "Source==ELiveLinkHubTimecodeSource::SystemTimeEditor"))
	FFrameRate DesiredFrameRate = FFrameRate(60, 1);

	/**
	 * Number of frames to subtract from the qualified frame time when GetDelayedQualifiedFrameTime or GetDelayedTimecode is called.
	 * @see GetDelayedQualifiedFrameTime, GetDelayedTimecode
	 */
	UPROPERTY(config, EditAnywhere, Category = "Timecode", meta = (ClampMin = "0", UIMin = "0", UIMax = "1200"))
	float FrameDelay = 0.f;

	/** The number of frame to keep in memory. The provider will not be synchronized until the buffer is full at least once. */
	UPROPERTY(config, EditAnywhere, Category = "Timecode", meta = (ClampMin = "2", UIMin = "2", ClampMax = "10", UIMax = "10"))
	int32 BufferSize = 2;

	/** 
	 * How timecode should be evaluated. 
	 * @Note: This will be overridden while a Live Link Hub recording is playing.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Timecode")
	ELiveLinkTimecodeProviderEvaluationType EvaluationType = ELiveLinkTimecodeProviderEvaluationType::Latest;

	/** If set, this will override the timecode evaluation type. */
	UPROPERTY(Transient)
	TOptional<ELiveLinkTimecodeProviderEvaluationType> OverrideEvaluationType;

	/** Assign the settings to a new timecode provider and override the current engine settings. */
	UE_API void AssignTimecodeSettingsAsProviderToEngine() const;
};

/** Status of a UE client connected to a live link hub. */
UENUM()
enum class ELiveLinkClientStatus
{
	Connected, /** Default state of a UE client. */
	Disconnected, /** Client is not connected to the hub. */
	Recording  /** UE is currently doing a take record. */
};

/** Information related to an unreal client that is connecting to a livelink hub instance. */
USTRUCT()
struct FLiveLinkClientInfoMessage
{
	GENERATED_BODY()

	/** Full name used to identify this client. (ie.UEFN_sessionID_LDN_WSYS_9999) */
	UPROPERTY()
	FString LongName;

	/** Status of the client, ie. is it actively doing a take record at the moment? */
	UPROPERTY()
	ELiveLinkClientStatus Status = ELiveLinkClientStatus::Disconnected;

	/** Name of the host of the UE client */
	UPROPERTY()
	FString Hostname;

	/** Name of the current project. */
	UPROPERTY()
	FString ProjectName;

	/** Name of the current level opened. */
	UPROPERTY()
	FString CurrentLevel;

	/** If this is representing a LiveLinkHub instance in Hub mode, this holds the LiveLink provider name, otherwise it's empty. */
	UPROPERTY()
	FString LiveLinkInstanceName;

	/** Whether the client is a hub or an unreal instance. */
	UPROPERTY()
	ELiveLinkTopologyMode TopologyMode = ELiveLinkTopologyMode::UnrealClient;

	/** LiveLink Version in use by this client. */
	UPROPERTY()
	int32 LiveLinkVersion = ILiveLinkClient::LIVELINK_VERSION;
};

/** Special connection message used when connecting to a livelink hub that contains information about this client. */
USTRUCT()
struct FLiveLinkHubConnectMessage
{
	GENERATED_BODY()

	/** Client information to forward to the hub */
	UPROPERTY()
	FLiveLinkClientInfoMessage ClientInfo;

	/** MessageBus Address of the control endpoint (Address of the LiveLinkHubMessagingModule's ControlChannel). */
	UPROPERTY()
	FString ControlEndpoint;

	/** ID of the message bus source. */
	UPROPERTY()
	FGuid SourceGuid;
};

/** Sent by UE/LLH to LLH instances to force them to populate their MessageBus AddressBook with the ControlEndpoint's address. */
USTRUCT()
struct FLiveLinkHubBeaconMessage
{
	GENERATED_BODY()

	/** Mode of the client that's sending this message. */
	UPROPERTY()
	ELiveLinkTopologyMode TopologyMode = ELiveLinkTopologyMode::Hub;

	/** Name of the machine that sent the beacon. */
	UPROPERTY()
	FString Hostname = FPlatformProcess::ComputerName();

	/** Name of the project of the client sending this message. */
	UPROPERTY()
	FString ProjectName;

	/** Name of the level of the client sending this message. */
	UPROPERTY()
	FString LevelName;

	/** Unique ID for a given client process. */
	UPROPERTY()
	FGuid InstanceId = FApp::GetInstanceId();
};

/** Special connection message used to tell a UE client or Hub that they should disconnect themselves. */
USTRUCT()
struct FLiveLinkHubDisconnectMessage
{
	GENERATED_BODY()

	/** Name of the provider to disconnect. */
	UPROPERTY()	
	FString ProviderName;

	/** Name of the machine that hosts the provider. */
	UPROPERTY()
	FString MachineName;

	/** Id of the source to disconnect. */
	UPROPERTY()
	FGuid SourceGuid;

};

/** Discovery message used by LiveLinkHubConnectionManager to find providers to connect to. */
USTRUCT()
struct FLiveLinkHubDiscoveryMessage
{
	GENERATED_BODY()

	FLiveLinkHubDiscoveryMessage() = default;

	FLiveLinkHubDiscoveryMessage(FString InProviderName, ELiveLinkTopologyMode InMode, const FLiveLinkHubInstanceId& InInstanceId)
		: ProviderName(MoveTemp(InProviderName))
		, Mode(InMode)
		, InstanceId(InInstanceId.ToString())
		, DiscoveryProtocolVersion(2) // Defaults to 2 when constructing manually.
	{
	}

	/** Name of the provider to connect. */
	UPROPERTY()
	FString ProviderName;

	/** Name of the provider to connect. */
	UPROPERTY()
	ELiveLinkTopologyMode Mode = ELiveLinkTopologyMode::Hub;

	/** Name of the machine that hosts the provider. */
	UPROPERTY()
	FString MachineName = FPlatformProcess::ComputerName();

	/** Unique ID for this provider. */
	UPROPERTY()
	FString InstanceId;

	/** Creation time used to calculate the machine time offset. */
	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	double CreationTime = FPlatformTime::Seconds();

	/** LiveLink Version in use by this client. */
	UPROPERTY()
	int32 LiveLinkVersion = ILiveLinkClient::LIVELINK_VERSION;

	/** LiveLink Discovery Protocol Version in use. Defaults to 1 when deserializing for backwards compatibility. */
	UPROPERTY()
	int32 DiscoveryProtocolVersion = 1;
};

/** Base for control messages for negotiating auxiliary channel endpoints. */
USTRUCT()
struct FLiveLinkHubAuxChannelBaseMessage
{
	GENERATED_BODY()

	/** Aux channel version. */
	UPROPERTY()
	int32 AuxProtocolVersion = 1;

	/** Identifier for the channel. */
	UPROPERTY()
	FGuid ChannelId;
};

/**
 * Sent to LLH control endpoint to negotiate additional auxiliary endpoints.
 * Subclass this struct to specialize it for your channel type, then:
 * 1) Register a handler for your derived channel request type via
 *    ILiveLinkHubMessagingModule::RegisterAuxChannelRequestHandler.
 * 2) Send a channel request via ILiveLinkHubClientsModel::RequestAuxiliaryChannel.
 */
USTRUCT()
struct FLiveLinkHubAuxChannelRequestMessage : public FLiveLinkHubAuxChannelBaseMessage
{
	GENERATED_BODY()
};

/** Send this from your aux channel handler in response to an accepted request. */
USTRUCT()
struct FLiveLinkHubAuxChannelAcceptMessage : public FLiveLinkHubAuxChannelBaseMessage
{
	GENERATED_BODY()
};

/**
 * Send this from your aux channel handler in response to a rejected request.
 * Also sent by the framework if no handler was registered for the request type.
 */
USTRUCT()
struct FLiveLinkHubAuxChannelRejectMessage : public FLiveLinkHubAuxChannelBaseMessage
{
	GENERATED_BODY()
};

/** Send this to close an existing aux channel. */
USTRUCT()
struct FLiveLinkHubAuxChannelCloseMessage : public FLiveLinkHubAuxChannelBaseMessage
{
	GENERATED_BODY()
};


#undef UE_API
