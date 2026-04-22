// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS_VISUAL_DEBUGGER

#include "ChaosVDRecordingDetails.h"
#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Containers/Ticker.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/TransactionallySafeRWLock.h"

#define UE_API CHAOSVDRUNTIME_API

namespace Chaos::VD
{
	class FRelayTraceWriter;
}

struct FChaosVDRecording;
class FText;

/* Option flags that controls what should be recorded when doing a full capture **/
enum class EChaosVDFullCaptureFlags : int32
{
	Geometry = 1 << 0,
	Particles = 1 << 1,
};
ENUM_CLASS_FLAGS(EChaosVDFullCaptureFlags)

DECLARE_MULTICAST_DELEGATE(FChaosVDRecordingStateChangedDelegate)
DECLARE_MULTICAST_DELEGATE_OneParam(FChaosVDCaptureRequestDelegate, EChaosVDFullCaptureFlags)
DECLARE_MULTICAST_DELEGATE_OneParam(FChaosVDRecordingStartFailedDelegate, const FText&)
DECLARE_DELEGATE_OneParam(FChaosVDRelayTraceDataDelegate, Chaos::VD::FRelayTraceWriter&)
DECLARE_DELEGATE_RetVal(FChaosVDTraceDetails, FChaosVDExternalTraceStatusDelegate)

DECLARE_LOG_CATEGORY_EXTERN(LogChaosVDRuntime, Log, All)

class FChaosVDRuntimeModule : public IModuleInterface
{
public:

	static UE_API FChaosVDRuntimeModule& Get();
	static UE_API bool IsLoaded();
	
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;

	/** Starts a CVD recording by starting a Trace session. It will stop any existing trace session
	 * @param Args : Arguments array usually provided by the commandline. Used to determine if we want to record to file or a local trace server
	 */
	UE_API void StartRecording(TConstArrayView<FString> Args);

	/** Starts a CVD recording by starting a Trace session. It will stop any existing trace session
	 * @param InRecordingStartCommand : Used to determine if we want to record to file or a local trace server among other settings
	 */
	UE_API void StartRecording(const FChaosVDStartRecordingCommandMessage& InRecordingStartCommand);
	
	/* Stops an active recording */
	UE_API void StopRecording();

	/** Returns true if we are currently recording a Physics simulation */
	bool IsRecording() const
	{
		return bIsRecording;
	}

	/** Returns a unique ID used to be used to identify CVD (Chaos Visual Debugger) data */
	UE_API int32 GenerateUniqueID();

	static FDelegateHandle RegisterRecordingStartedCallback(const FChaosVDRecordingStateChangedDelegate::FDelegate& InCallback)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStartedDelegate.Add(InCallback);
	}

	static FDelegateHandle RegisterPostRecordingStartedCallback(const FChaosVDRecordingStateChangedDelegate::FDelegate& InCallback)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return PostRecordingStartedDelegate.Add(InCallback);
	}

	static FDelegateHandle RegisterRecordingStopCallback(const FChaosVDRecordingStateChangedDelegate::FDelegate& InCallback)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStopDelegate.Add(InCallback);
	}

	static FDelegateHandle RegisterRecordingStartFailedCallback(const FChaosVDRecordingStartFailedDelegate::FDelegate& InCallback)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStartFailedDelegate.Add(InCallback);
	}
	
	static FDelegateHandle RegisterFullCaptureRequestedCallback(const FChaosVDCaptureRequestDelegate::FDelegate& InCallback)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return PerformFullCaptureDelegate.Add(InCallback);
	}

	static bool RemoveRecordingStartedCallback(const FDelegateHandle& InDelegateToRemove)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStartedDelegate.Remove(InDelegateToRemove);
	}
	
	static bool RemovePostRecordingStartedCallback(const FDelegateHandle& InDelegateToRemove)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return PostRecordingStartedDelegate.Remove(InDelegateToRemove);
	}

	static bool RemoveRecordingStopCallback(const FDelegateHandle& InDelegateToRemove)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStopDelegate.Remove(InDelegateToRemove);
	}

	static bool RemoveRecordingStartFailedCallback(const FDelegateHandle& InDelegateToRemove)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return RecordingStartFailedDelegate.Remove(InDelegateToRemove);
	}
	
	static bool RemoveFullCaptureRequestedCallback(const FDelegateHandle& InDelegateToRemove)
	{
		UE::TWriteScopeLock WriteLock(DelegatesRWLock);
		return PerformFullCaptureDelegate.Remove(InDelegateToRemove);
	}

	/** Returns the accumulated recording time in seconds since the recording started */
	float GetAccumulatedRecordingTime() const
	{
		return AccumulatedRecordingTime;
	}

	/** [Deprecated] Returns the full path of the active recording file*/
	UE_DEPRECATED(5.7, "This method is no longer used. Use GetCurrentTraceSessionDetails")
	UE_API FString GetLastRecordingFileNamePath() const;

	/** Return the current connection details for the underlying trace session */
	UE_API FChaosVDTraceDetails GetCurrentTraceSessionDetails() const;

	EChaosVDRecordingMode GetCurrentRecordingMode() const
	{
		return CurrentRecordingMode;
	}

	/** Finds a valid file name for a new file - Used to generate the file name for the Trace recording */
	UE_API void GenerateRecordingFileName(FString& OutFileName);

	/**
	 * Registers an external handler that will be in charge of relaying the traced data to a remote CVD instance if the relay transport mode is selected
	 * @param InExternalRelayExecutor Delegate that will be executed to relay data to a remote CVD instance, periodically
	 */
	UE_API void RegisterExternalRelayExecutor(const FChaosVDRelayTraceDataDelegate& InExternalRelayExecutor);

	/**
	 * Unbinds the delegate to an external relay executor
	 */
	UE_API void UnregisterCurrentExternalRelayExecutor();

	/**
	 * Registers an external handler that will be in charge of providing the trace status if the recording mode is set to Relay
	 * @param InExternalTraceStatusCallback Delegate that will be executed to get the trace status, periodically
	 */
	UE_API void RegisterExternalTraceStatusProvider(const FChaosVDExternalTraceStatusDelegate& InExternalTraceStatusCallback);

	/**
	 * Unbinds the delegate to the current external trace status provider
	 */
	UE_API void UnregisterExternalTraceStatusProvider();

	FSimpleMulticastDelegate& OnTraceConnectionDetailsUpdated()
	{
		return TraceConnectionDetailsUpdatedDelegate;
	}

private:

	void RelayTraceData();

	void SetupRelayDataPumpDelegates();
	void ClearRelayDataPumpDelegates();

	/** Stops the current Trace session */
	UE_API void StopTrace();

	/** Queues a full Capture of the simulation on the next frame */
	UE_API bool RequestFullCapture(float DeltaTime);

	/** Queues a full Capture of the simulation on the next frame */
	UE_API bool RecordingTimerTick(float DeltaTime);
	
	void HandleTraceConnectionEstablished();

	/** Used to handle stop requests to the active trace session that were not done by us
	 * That is a possible scenario because Trace is shared by other In-Editor tools
	 */
	UE_API void HandleTraceStopRequest(FTraceAuxiliary::EConnectionType TraceType, const FString& TraceDestination);

	UE_API bool WaitForTraceSessionDisconnect();

	UE_API void EnableRequiredTraceChannels();
	UE_API void SaveAndDisabledCurrentEnabledTraceChannels();
	UE_API void RestoreTraceChannelsToPreRecordingState();

	bool bIsRecording = false;
	bool bRequestedStop = false;

	float AccumulatedRecordingTime = 0.0f;

	FTSTicker::FDelegateHandle FullCaptureRequesterHandle;
	FTSTicker::FDelegateHandle RecordingTimerHandle;
	FTSTicker::FDelegateHandle RelayExecutorTickerHandle;

	FDelegateHandle OnConnectionDelegateHandle;
	FDelegateHandle OnTraceStoppedDelegateHandle;

	static UE_API FChaosVDRecordingStateChangedDelegate RecordingStartedDelegate;
	static UE_API FChaosVDRecordingStateChangedDelegate PostRecordingStartedDelegate;
	static UE_API FChaosVDRecordingStateChangedDelegate RecordingStopDelegate;
	static UE_API FChaosVDRecordingStartFailedDelegate RecordingStartFailedDelegate;
	static UE_API FChaosVDCaptureRequestDelegate PerformFullCaptureDelegate;

	std::atomic<int32> LastGeneratedID;

	TMap<FString, bool> OriginalTraceChannelsState;

	EChaosVDRecordingMode CurrentRecordingMode = EChaosVDRecordingMode::Invalid;
	EChaosVDTransportMode CurrentTransportMode = EChaosVDTransportMode::Invalid;

	TUniquePtr<Chaos::VD::FRelayTraceWriter> RelayWriter;
	FChaosVDRelayTraceDataDelegate ExternalRelayExecutorDelegate;

	static UE_API FTransactionallySafeRWLock DelegatesRWLock;

	FChaosVDExternalTraceStatusDelegate ExternalTraceStatusDelegate;

	FSimpleMulticastDelegate TraceConnectionDetailsUpdatedDelegate;
};
#endif

#undef UE_API 