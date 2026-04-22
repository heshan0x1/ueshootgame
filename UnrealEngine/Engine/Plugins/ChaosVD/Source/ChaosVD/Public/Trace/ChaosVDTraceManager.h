// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDModule.h"
#include "Chaos/Serialization/SerializedDataBuffer.h"
#include "Containers/SpscQueue.h"
#include "Containers/StringFwd.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Templates/SharedPointer.h"
#include "Trace/StoreClient.h"

struct FChaosVDRelayTraceDataMessage;
enum class EChaosVDLoadRecordedDataMode : uint8;
class FChaosVDEngine;
class FChaosVDTraceModule;

namespace TraceServices { class IAnalysisSession; }

namespace Chaos::VD
{
	/** Object that allows showing a progress bar in the status bar of the editor,
	 * where the progress can be updated from any thread */
	struct FAsyncProgressNotification : FTSTickerObjectBase
	{
		FAsyncProgressNotification(const FText& InMessage) : NotificationTitle(InMessage)
		{
		}


		virtual ~FAsyncProgressNotification() override;

		/** Updates the current progress
		 * @param InCurrentProgress Value between 0 and 100, representing the progress we want to display
		 */
		void EnterProgress(int32 InCurrentProgress);

	private:
		virtual bool Tick(float DeltaTime) override;

		float ElapsedTimeSinceLastUpdate = 0.0f;

		FProgressNotificationHandle NotificationHandle;
		std::atomic<int32> CurrentProgress = 0;
		FText NotificationTitle;
	};

	/** A trace data stream object that listens for data coming from a relay stream
	 * via CVD's Engine Editor Bridge object */
	class FRelayDataStream : public UE::Trace::IInDataStream
	{
	public:
		explicit FRelayDataStream(FGuid InRemoteSessionID);

		virtual ~FRelayDataStream() override;

		void EnqueueRelayedData(const TConstArrayView<uint8> InTraceDataBuffer);
		void EnqueueRelayedData(TArray<uint8>&& InTraceDataBuffer);

	private:
		virtual int32 Read(void* Dest, uint32 DestSize) override;

		virtual void Close() override;
		virtual bool WaitUntilReady() override;

		TSpscQueue<FSerializedDataBuffer> DataQueue;
		FEvent*	CancelEvent = nullptr;
		TOptional<FSerializedDataBuffer> CurrentPacket;
		uint32 CurrentPacketOffset = 0;
		bool bClosed = false;

		FGuid RemoteSessionID = FGuid();
	};
}

/**
 * Structure containing info about a trace session used by Chaos Visual Debugger
 */
struct FChaosVDTraceSessionDescriptor
{
	FString SessionName;
	uint16 SessionPort = 0;
	FGuid RemoteSessionID;
	bool bIsLiveSession = false;

	bool IsValid() const { return !SessionName.IsEmpty(); }
};

/** Objects that allows us to use TLS to temporarily store and access a ptr to an existing instance.
 * This is temporary to workaround the lack of an API method we need in the trace API, and will be removed in the future.
 * either when we add that to the API, or find another way to pass an existing CVD recording to the trace provider before anaislis starts
 */
class FChaosVDTraceManagerThreadContext : public TThreadSingleton<FChaosVDTraceManagerThreadContext>
{
public:

	TWeakPtr<FChaosVDRecording> PendingExternalRecordingWeakPtr;
};

/** Manager class used by Chaos VD to interact/control UE Trace systems */
class FChaosVDTraceManager
{
public:
	FChaosVDTraceManager();
	~FChaosVDTraceManager();

	/** Load a trace file and starts analyzing it
	 * @param InTraceFilename File Name including (Path Included) of the Trace file to load
	 */
	CHAOSVD_API FString LoadTraceFile(const FString& InTraceFilename, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr = nullptr);

	FString LoadTraceFile(TUniquePtr<IFileHandle>&& InFileHandle, const FString& InTraceSessionName, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr);
	
	/**
	 * Connects to a live Trace Session and starts analyzing it.
	 * @param InSessionHost Trace Store Address for this session
	 * @param SessionID Trace ID in the Trace Store provided as host
	 * @param ExistingRecordingPtr Ptr to an existing recording instance to append the new CVD data to, if any
	 * @return Session Name
	 */
	FString ConnectToLiveSession(FStringView InSessionHost, uint32 SessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr = nullptr);

	/**
	 * Opens up a direct trace stream and waits for data
	 * @param OutSessionPort Number of the port we open for this session
	 * @param ExistingRecordingPtr Ptr to an existing recording instance to append the new CVD data to, if any
	 * @return Session Name
	 */
	FString ConnectToLiveSession_Direct(uint16& OutSessionPort, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr = nullptr);

	/**
	 * Opens up a relay trace stream and waits for data
	 * @param RemoteSessionID Session ID to which we want to connect to
	 * @param ExistingRecordingPtr Ptr to an existing recording instance to append the new CVD data to, if any
	 * @return Session Name
	 */
	FString ConnectToLiveSession_Relay(FGuid RemoteSessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr = nullptr);

	/** Returns the path to the local trace store */
	FString GetLocalTraceStoreDirPath();

	/** Returns a ptr to the session registered with the provided session name. Null if no session is found */
	CHAOSVD_API TSharedPtr<const TraceServices::IAnalysisSession> GetSession(const FString& InSessionName);

	/** Stops and de-registers a trace session registered with the provided session name */
	CHAOSVD_API void CloseSession(const FString& InSessionName);

	/** Stops a trace session registered with the provided session name */
	void StopSession(const FString& InSessionName);

	template<typename TVisitor>
	static void EnumerateActiveSessions(FStringView InSessionHost, TVisitor Callback);

	static const UE::Trace::FStoreClient::FSessionInfo* GetTraceSessionInfo(FStringView InSessionHost, FGuid TraceGuid);

	template<typename TVisitor>
	void EnumerateActiveSessions(TVisitor Callback);

	/** Access the trace store at the provided host address, and returns the file name for the trace file
	 * @param InSessionHost Address of the Trace Store
	 * @param SessionID Session id of the trace data from which get the file name from
	 * @return Full Filename with path to the trace file if exist. Returns an empty string if a trace file cannot be found
	 */
	FString GetTraceFileNameFromStoreForSession(FStringView InSessionHost, uint32 SessionID);

	/** Access the trace store at the provided host address, and returns a trace data stream ready to use
	 * @param InSessionHost Address of the Trace Store
	 * @param SessionID Session id of the trace data
	 */
	UE::Trace::FStoreClient::FTraceData GetTraceDataStreamFromStore(FStringView InSessionHost, uint32 SessionID);

private:

	TUniquePtr<FArchiveFileWriterGeneric>  OpenTraceFileForWrite();

	bool ConnectToLiveSession_Internal(uint32 SessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr, const FString& InRequestedSessionName, UE::Trace::FStoreClient::FTraceData&& InTraceDataStream);

	TUniquePtr<UE::Trace::IInDataStream> CreateFileDataStream(TUniquePtr<IFileHandle>&& InFileHandle);
	TUniquePtr<Chaos::VD::FRelayDataStream> CreateRelayDataStream(FGuid RemoteSessionID);

	/** Temporary workaround method to set an existing recording structure in CVD's trace provider before the trace analysis starts
	 * in the Trace analysis thread.*/
	void SetPendingExternalRecordingToProcess(const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr);

	/** The trace analysis session. */
	TMap<FString, TSharedPtr<const TraceServices::IAnalysisSession>> AnalysisSessionByName;

	TSharedPtr<FChaosVDTraceModule> ChaosVDTraceModule;
};

template <typename TCallback>
void FChaosVDTraceManager::EnumerateActiveSessions(FStringView InSessionHost, TCallback Callback)
{
	if (InSessionHost.IsEmpty())
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to connect to trace store. Provided session host is empty"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	using namespace UE::Trace;
	const FStoreClient* StoreClient = FStoreClient::Connect(InSessionHost.GetData());

	if (!StoreClient)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to connect to trace store at [%s]"), ANSI_TO_TCHAR(__FUNCTION__), InSessionHost.GetData())
		return;
	}

	const uint32 SessionCount = StoreClient->GetSessionCount();

	for (uint32 SessionIndex = 0; SessionIndex < SessionCount; SessionIndex++)
	{
		if (const FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfo(SessionIndex))
		{
			if (!Callback(*SessionInfo))
			{
				return;
			}
		}	
	}
}

template <typename TVisitor>
void FChaosVDTraceManager::EnumerateActiveSessions(TVisitor Callback)
{
	for (const TPair<FString, TSharedPtr<const TraceServices::IAnalysisSession>>& SessionWithName : AnalysisSessionByName)
	{
		if (SessionWithName.Value)
		{
			if (!Callback(SessionWithName.Value.ToSharedRef()))
			{
				return;
			}
		}
	}
}
