// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkHubClientsModel.h"
#include "LiveLinkHubMessages.h"

#include "LiveLinkHubUEClientInfo.generated.h"


/** Wrapper around FLiveLinkClientInfoMessage that adds additional info. Used mainly to display information about a client in the UI. */
USTRUCT()
struct FLiveLinkHubUEClientInfo
{
	GENERATED_BODY();

	FLiveLinkHubUEClientInfo() = default;

	explicit FLiveLinkHubUEClientInfo(const FLiveLinkClientInfoMessage& InClientInfo, FLiveLinkHubClientId InClientId = FLiveLinkHubClientId::NewId())
		: Id(InClientId)
		, LongName(InClientInfo.LongName)
		, Status(InClientInfo.Status)
		, IPAddress(TEXT("192.168.0.1 (Placeholder)"))
		, Hostname(InClientInfo.Hostname)
		, ProjectName(InClientInfo.ProjectName)
		, CurrentLevel(InClientInfo.CurrentLevel)
		, LiveLinkInstanceName(InClientInfo.LiveLinkInstanceName)
		, TopologyMode(InClientInfo.TopologyMode)
	{
	}

	void UpdateFromInfoMessage(const FLiveLinkClientInfoMessage& InClientInfo)
	{
		LongName = InClientInfo.LongName;
		Status = InClientInfo.Status;
		IPAddress = TEXT("192.168.0.1 (Placeholder)");
		Hostname = InClientInfo.Hostname;
		ProjectName = InClientInfo.ProjectName;
		CurrentLevel = InClientInfo.CurrentLevel;
	}

	/** Identifier for this client. */
	UPROPERTY()
	FLiveLinkHubClientId Id;

	/** Full name used to identify this client. (ie.UEFN_sessionID_LDN_WSYS_9999) */
	UPROPERTY(VisibleAnywhere, Category = "Client")
   	FString LongName;
	
	/** Status of the client, ie. is it actively doing a take record at the moment? */
	UPROPERTY(transient)
	ELiveLinkClientStatus Status = ELiveLinkClientStatus::Disconnected;
	
	UPROPERTY(VisibleAnywhere, Category = "Client", meta = (DisplayName = "IP Address"))
	FString IPAddress;
	
	/** Name of the host of the UE client */
	UPROPERTY(VisibleAnywhere, Category = "Client")
	FString Hostname;

	/** Name of the current project. */
	UPROPERTY(VisibleAnywhere, Category = "Client")
	FString ProjectName;
	
	/** Name of the current level opened. */
	UPROPERTY(VisibleAnywhere, Category = "Client")
	FString CurrentLevel;

	/** If this is representing a LiveLinkHub instance in Hub mode, this holds the LiveLink provider name, otherwise it's empty. */
	UPROPERTY()
	FString LiveLinkInstanceName;

	/** Whether the client is a hub or an unreal instance. */
	UPROPERTY()
	ELiveLinkTopologyMode TopologyMode = ELiveLinkTopologyMode::UnrealClient;
	
	/** Subjects that should not be transmitted to this client. */
	UPROPERTY()
	TSet<FName> DisabledSubjects;
	
	/** Whether this client should receive messages. */
	UPROPERTY()
	bool bEnabled = true;
};
