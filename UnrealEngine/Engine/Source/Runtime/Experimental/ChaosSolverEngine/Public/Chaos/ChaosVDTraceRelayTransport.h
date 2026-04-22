// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "ChaosVDTraceRelayTransport.generated.h"

namespace Chaos::VD
{

/** Structure containing the details necessary to connect to a relay instance */
USTRUCT()
struct FRelayConnectionInfo
{
	GENERATED_BODY()

	/** Relay IP address */
	UPROPERTY()
	FString Address;

	/** Relay port */
	UPROPERTY()
	uint16 Port = 0;

	/** Encoded SSL certificate */
	UPROPERTY()
	TArray<uint8> CertificateAuthority;
};
}

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "Delegates/Delegate.h"

struct FChaosVDRelayTraceDataMessage;
struct FGuid;

namespace Chaos::VD
{
class FRelayTraceWriter;

/** Possible connection attempt results */
enum EConnectionAttemptResult
{
	NotStarted,
	InProgress,
	Success,
	Failed
};

/**
 * Interface for any CVD trace relay transport implementation
 */
class ITraceDataRelayTransport
{
public:
	virtual ~ITraceDataRelayTransport() = default;

	virtual void Initialize() = 0;
	virtual void Shutdown() = 0;

	DECLARE_DELEGATE_OneParam(FProcessReceivedRelayDataDelegate, const TConstArrayView<uint8> DataPacket);
	virtual void RegisterRelayDataReceiverForSessionID(FGuid RemoteSessionID, const FProcessReceivedRelayDataDelegate& InProcessDataDelegate) = 0;
	virtual void UnregisterRelayDataReceiverForSessionID(FGuid RemoteSessionID) = 0;

	virtual void RelayTraceDataFromWriter(FRelayTraceWriter& InRelayTraceWriter) = 0;

	virtual FRelayConnectionInfo GetConnectionInfo() = 0;
	virtual EConnectionAttemptResult ConnectToRelay(FGuid RemoteSessionID, const FRelayConnectionInfo ConnectionInfo) = 0;
	virtual EConnectionAttemptResult GetConnectionAttemptResult(FGuid RemoteSessionID) = 0;
};

}

#endif // WITH_CHAOS_VISUAL_DEBUGGER
