// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Misc/Guid.h"
#include "ChaosVDRecordingDetails.generated.h"

UENUM()
enum class EChaosVDRecordingMode : uint8
{
	Invalid,
	Live,
	File
};

/** Available transport modes for CVD Trace Data*/
UENUM()
enum class EChaosVDTransportMode : uint8
{
	Invalid,
	/** The data will be traced directly to a file in the local file system */
	FileSystem,
	/** Data will be traced to the selected trace store server */
	TraceServer,
	/** Data will be traced directly to the editor, accessible via sockets */
	Direct,
	/** Data will be traced via the trace relay system using a custom transport, like normal UE Networking or a direct socket connection */
	Relay
};

USTRUCT()
struct FChaosVDStartRecordingCommandMessage
{
	GENERATED_BODY()

	UPROPERTY()
	EChaosVDRecordingMode RecordingMode = EChaosVDRecordingMode::Invalid;

	UPROPERTY()
	EChaosVDTransportMode TransportMode = EChaosVDTransportMode::Invalid;
	
	UPROPERTY()
	FString Target;

	UPROPERTY()
	TArray<FString> DataChannelsEnabledOverrideList;
};

/** Structure containing the info necessary to connect to a trace session and start receiving data */
USTRUCT()
struct FChaosVDTraceDetails
{
	GENERATED_BODY()

	FChaosVDTraceDetails() = default;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FChaosVDTraceDetails(const FChaosVDTraceDetails& Other) = default;
	FChaosVDTraceDetails(FChaosVDTraceDetails&& Other) noexcept = default;
	FChaosVDTraceDetails& operator=(const FChaosVDTraceDetails& Other) = default;
	FChaosVDTraceDetails& operator=(FChaosVDTraceDetails&& Other) noexcept = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	void MarkAsValid()
	{
		bIsValid = true;
	}

	bool IsValid() const
	{
		return bIsValid;
	}

	/** ID used to find the active trace session in the trace system */
	UPROPERTY()
	FGuid TraceGuid;

	/** ID from the CVD remote session from which this connection details come from */
	UPROPERTY()
	FGuid SessionGuid;

	/** Address of the trace server or editor in direct trace mode that will receive the trace data */
	UPROPERTY()
	FString TraceTarget;

	/*** Port number used for the trace connection, if any*/
	UPROPERTY()
	uint16 Port = 0;

	UE_DEPRECATED(5.7, "This property is no longer used and it will removed in the future")
	bool bIsConnected = false;

	/** Which recording mode is being used by this session (Live or File)*/
	UPROPERTY()
	EChaosVDRecordingMode Mode = EChaosVDRecordingMode::Invalid;

	/** How the data is being transported to the editor (Trace Server, Custom Relay, Direct, etc.)*/
	UPROPERTY()
	EChaosVDTransportMode TransportMode = EChaosVDTransportMode::Invalid;

	/** Encoded SSL certificate for the trace connection, if any */
	UPROPERTY()
	TArray<uint8> CertAuth;

	UPROPERTY()
	bool bIsValid = false;
};