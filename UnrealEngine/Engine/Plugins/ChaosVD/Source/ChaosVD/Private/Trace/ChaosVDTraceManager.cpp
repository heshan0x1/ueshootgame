// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/ChaosVDTraceManager.h"

#include "ChaosVDEngine.h"
#include "ChaosVDModule.h"
#include "ChaosVDRuntimeModule.h"
#include "ChaosVDSettingsManager.h"
#include "Chaos/ChaosVDEngineEditorBridge.h"
#include "Chaos/ChaosVDTraceRelayTransport.h"
#include "Chaos/Serialization/SerializedDataBuffer.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformFileManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Settings/ChaosVDGeneralSettings.h"
#include "Trace/ChaosVDTraceModule.h"
#include "Trace/StoreClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace Chaos::VD
{
	namespace CVars
	{
		static float SecondsToSleepWaitingForRelayData = 0.1f;
		static FAutoConsoleVariableRef CVarUseCVDDynamicMeshGenerator(
			TEXT("p.Chaos.VD.Tool.SecondsToSleepWaitingForRelayData"),
			SecondsToSleepWaitingForRelayData,
			TEXT("Time in seconds the trace analysis thread should wait to receive new data from a relay connection"));
	}

	FAsyncProgressNotification::~FAsyncProgressNotification()
	{
		if (NotificationHandle.IsValid())
		{
			// FSlateNotificationManager is not thread safe, so we need to make sure this runs in the game thread
			if (IsInGameThread())
			{
				FSlateNotificationManager::Get().CancelProgressNotification(NotificationHandle);
			}
			else
			{
				constexpr float Delay = 0.0f;
                FTSTicker::GetCoreTicker().AddTicker(TEXT("CancelCVDAsyncNotification"), Delay, [HandleCopy = NotificationHandle](float DeltaTime)
                {
                	FSlateNotificationManager::Get().CancelProgressNotification(HandleCopy);
                	return false;
                });
			}
		}
	}

	void FAsyncProgressNotification::EnterProgress(int32 InCurrentProgress)
	{
		CurrentProgress = InCurrentProgress;
	}

	bool FAsyncProgressNotification::Tick(float DeltaTime)
	{
		constexpr int32 MaxProgress = 100;
		constexpr float TimeBetweenUpdatesSeconds = 0.5f;

		ElapsedTimeSinceLastUpdate += DeltaTime;
		if (ElapsedTimeSinceLastUpdate < TimeBetweenUpdatesSeconds)
		{
			return true;
		}

		ElapsedTimeSinceLastUpdate = 0.0f;
		
		if (CurrentProgress >= MaxProgress)
		{
			if (NotificationHandle.IsValid())
			{
				FSlateNotificationManager::Get().UpdateProgressNotification(NotificationHandle, MaxProgress, MaxProgress, NotificationTitle);
			}
			NotificationHandle = FProgressNotificationHandle();

			// We are done and we can stop ticking
			return false;
		}

		if (!NotificationHandle.IsValid())
		{
			NotificationHandle = FSlateNotificationManager::Get().StartProgressNotification(NotificationTitle, MaxProgress);
		}
		else
		{
			FSlateNotificationManager::Get().UpdateProgressNotification(NotificationHandle, CurrentProgress, MaxProgress, NotificationTitle);
		}

		return true;
	}

	FRelayDataStream::FRelayDataStream(FGuid InRemoteSessionID)
		: CancelEvent(FPlatformProcess::GetSynchEventFromPool(false)), RemoteSessionID(InRemoteSessionID)
	{
#if WITH_CHAOS_VISUAL_DEBUGGER
		TSharedPtr<ITraceDataRelayTransport> RelayTransportInstance = FChaosVDEngineEditorBridge::Get().GetRelayTransportInstance();
		if (ensure(RelayTransportInstance))
		{
			RelayTransportInstance->RegisterRelayDataReceiverForSessionID(RemoteSessionID, ITraceDataRelayTransport::FProcessReceivedRelayDataDelegate::CreateRaw(this, &FRelayDataStream::EnqueueRelayedData));
		}
#endif
	}

	FRelayDataStream::~FRelayDataStream()
	{
		FPlatformProcess::ReturnSynchEventToPool(CancelEvent);

#if WITH_CHAOS_VISUAL_DEBUGGER
		TSharedPtr<ITraceDataRelayTransport> RelayTransportInstance = FChaosVDEngineEditorBridge::Get().GetRelayTransportInstance();
		if (RelayTransportInstance)
		{
			RelayTransportInstance->UnregisterRelayDataReceiverForSessionID(RemoteSessionID);
		}	
#endif
	}

	void FRelayDataStream::EnqueueRelayedData(const TConstArrayView<uint8> InTraceDataBuffer)
	{
		FSerializedDataBuffer DataBuffer(InTraceDataBuffer);
		DataQueue.Enqueue(MoveTemp(DataBuffer));
	}

	void FRelayDataStream::EnqueueRelayedData(TArray<uint8>&& InTraceDataBuffer)
	{
		FSerializedDataBuffer DataBuffer(MoveTemp(InTraceDataBuffer));
		DataQueue.Enqueue(MoveTemp(DataBuffer));
	}

	int32 FRelayDataStream::Read(void* Dest, uint32 DestSize)
	{
		if (DestSize == 0)
		{
			return 0;
		}

		if (Dest == nullptr)
		{
			return 0;
		}

		if (!ensure(CancelEvent))
		{
			return 0;
		}

		uint8* DestBuffer = static_cast<uint8*>(Dest);
		uint32 BytesCopied = 0;

		// We need to wait for some data to be queued. Returning 
		// zero bytes read is interpreted as eof.
		while (!CancelEvent->Wait(0, true))
		{
			if (!CurrentPacket)
			{
				CurrentPacket = DataQueue.Dequeue();
				CurrentPacketOffset = 0;
			}

			// Consume as many queued packets as possible and copy into
			// the destination buffer
			while (CurrentPacket && BytesCopied < DestSize)
			{
				const TArray<uint8>& SrcBuffer = CurrentPacket->GetDataAsByteArrayRef();
				if (SrcBuffer.IsEmpty())
				{
					// We should never get an empty packet. From this point on we can't trust the integrity of the data
					// so we need to close the stream
					Close();
					break;
				}

				const uint32 ReceivedBufferSize = static_cast<uint32>(SrcBuffer.Num());

				// Copy as much of the current buffer as possible into the remaining space 
				// in the destination buffer
				uint32 BytesToCopy = FMath::Min(ReceivedBufferSize - CurrentPacketOffset, DestSize - BytesCopied);
				FMemory::Memcpy(&DestBuffer[BytesCopied], SrcBuffer.GetData() + CurrentPacketOffset, BytesToCopy);
				BytesCopied += BytesToCopy;
				CurrentPacketOffset += BytesToCopy;

				if (CurrentPacketOffset == ReceivedBufferSize)
				{
					// Current packet buffer has been fully copied, goto next
					CurrentPacket = DataQueue.Dequeue();
					CurrentPacketOffset = 0;
				}
			}

			// If at least some bytes have been written return those. Otherwise, 
			// we'll sleep and wait for some data to arrive.
			if (BytesCopied > 0)
			{
				break;
			}

			FPlatformProcess::Sleep(CVars::SecondsToSleepWaitingForRelayData);
		}

		return BytesCopied;
	}

	void FRelayDataStream::Close()
	{
		if (CancelEvent)
		{
			CancelEvent->Trigger();
		}

		bClosed = true;
	}

	bool FRelayDataStream::WaitUntilReady()
	{		
		return DataQueue.Peek() != nullptr; 
	}
}

FChaosVDTraceManager::FChaosVDTraceManager() 
{
	ChaosVDTraceModule = MakeShared<FChaosVDTraceModule>();
	IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, ChaosVDTraceModule.Get());

	FString ChannelNameFString(TEXT("ChaosVD"));
	UE::Trace::ToggleChannel(ChannelNameFString.GetCharArray().GetData(), true);
}

FChaosVDTraceManager::~FChaosVDTraceManager()
{
	IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, ChaosVDTraceModule.Get());
}

FString FChaosVDTraceManager::LoadTraceFile(const FString& InTraceFilename, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	CloseSession(InTraceFilename);

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	
	constexpr bool bAllowWrite = false;
	IFileHandle* Handle = FileSystem.OpenRead(*InTraceFilename, bAllowWrite);
	if (!Handle)
	{
		return FString();
	}

	return LoadTraceFile(TUniquePtr<IFileHandle>(Handle), InTraceFilename, ExistingRecordingPtr);
}

FString FChaosVDTraceManager::LoadTraceFile(TUniquePtr<IFileHandle>&& InFileHandle, const FString& InTraceSessionName, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	if (InFileHandle->Size() == 0)
	{
		return FString();
	}

	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	if (const TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService = TraceServicesModule.GetAnalysisService())
	{
		SetPendingExternalRecordingToProcess(ExistingRecordingPtr);

		if (const TSharedPtr<const TraceServices::IAnalysisSession> NewSession = TraceAnalysisService->StartAnalysis(~0, *InTraceSessionName, CreateFileDataStream(MoveTemp(InFileHandle))))
		{
			AnalysisSessionByName.Add(InTraceSessionName, NewSession.ToSharedRef());

			return NewSession->GetName();
		}
	}

	return FString();
}

const UE::Trace::FStoreClient::FSessionInfo* FChaosVDTraceManager::GetTraceSessionInfo(FStringView InSessionHost, FGuid TraceGuid)
{
	if (InSessionHost.IsEmpty())
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to connect to trace store. Provided session host is empty"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	using namespace UE::Trace;
	const FStoreClient* StoreClient = FStoreClient::Connect(InSessionHost.GetData());

	if (!StoreClient)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to connect to trace store at [%s]"), ANSI_TO_TCHAR(__FUNCTION__), InSessionHost.GetData())
		return nullptr;
	}

	return StoreClient->GetSessionInfoByGuid(TraceGuid);
}

FString FChaosVDTraceManager::GetTraceFileNameFromStoreForSession(FStringView InSessionHost, uint32 SessionID)
{
	using namespace UE::Trace;

	if (InSessionHost.IsEmpty())
	{
		return FString();
	}
	
	FStoreClient* StoreClient = FStoreClient::Connect(InSessionHost.GetData());
	if (!StoreClient)
	{
		return FString();
	}

	// Note: The following calls to the store client are scoped because FStoreClient::FTraceInfo and FStoreClient::FStatus share the same buffer
	// under the hood, therefor although the ptr will still be valid, the data will not after we obtain one after the other
	FString FileName;
	{
		const FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(SessionID);
		if (!TraceInfo)
		{
			return FString();
		}

		const FUtf8StringView Utf8NameView = TraceInfo->GetName();
		FileName = FString(Utf8NameView);
		if (!FileName.EndsWith(TEXT(".utrace")))
		{
			FileName += TEXT(".utrace");
		}
	}

	FString TraceStorePath;
	{
		const FStoreClient::FStatus* StoreStatus = StoreClient->GetStatus();
		if (!StoreStatus)
		{
			return FString();
		}

		TraceStorePath = FString(StoreStatus->GetStoreDir());
	}
	
	FString FullFileName = FPaths::Combine(TraceStorePath, FileName);
	FPaths::NormalizeFilename(FullFileName);

	return FullFileName;
}


UE::Trace::FStoreClient::FTraceData FChaosVDTraceManager::GetTraceDataStreamFromStore(FStringView InSessionHost, uint32 SessionID)
{
	using namespace UE::Trace;
	FStoreClient* StoreClient = FStoreClient::Connect(InSessionHost.GetData());
	if (!StoreClient)
	{
		return UE::Trace::FStoreClient::FTraceData();
	}

	return StoreClient->ReadTrace(SessionID);
}


TUniquePtr<UE::Trace::IInDataStream> FChaosVDTraceManager::CreateFileDataStream(TUniquePtr<IFileHandle>&& InFileHandle)
{
	check(InFileHandle);

	struct FFileDataStream : public UE::Trace::IInDataStream
	{
		explicit FFileDataStream(TUniquePtr<IFileHandle>&& FileHandle) :
			Handle(MoveTemp(FileHandle)),
			AsyncProgressNotification(Chaos::VD::FAsyncProgressNotification(NSLOCTEXT("ChaosVisualDebugger", "FileLoadingProgressNotification","Loading File")))
		{
			check(Handle);
			FileSize = Handle->Size();
			RemainingDataSize = FileSize;
			constexpr int32 FileFullyLoadedPercentage = 100;

			check(FileSize > 0);
			ProgressPercentagePerByte = static_cast<double>(FileFullyLoadedPercentage) / static_cast<double>(FileSize);
		}
		
		virtual int32 Read(void* Data, uint32 Size) override
		{
			if (RemainingDataSize <= 0)
			{
				return 0;
			}

			if (Size > RemainingDataSize)
			{
				Size = static_cast<uint32>(RemainingDataSize);
			}
		
			RemainingDataSize -= Size;

			double ProgressPercentage = static_cast<double>(FileSize - RemainingDataSize) * ProgressPercentagePerByte;
			AsyncProgressNotification.EnterProgress(FMath::CeilToInt32(ProgressPercentage));

			check(Handle);
			if (!Handle->Read(static_cast<uint8*>(Data), Size))
			{
				return 0;
			}
			return Size;
		}

		virtual void Close() override
		{
			Handle.Reset();
		}

	private:
		
		TUniquePtr<IFileHandle> Handle;
		uint64 RemainingDataSize = 0;
		uint64 FileSize = 0;
		double ProgressPercentagePerByte = 0.0;
	
		Chaos::VD::FAsyncProgressNotification AsyncProgressNotification;
	};

	FFileDataStream* FileStream = new FFileDataStream(MoveTemp(InFileHandle));

	TUniquePtr<UE::Trace::IInDataStream> DataStream(FileStream);

	return DataStream;
}

TUniquePtr<Chaos::VD::FRelayDataStream> FChaosVDTraceManager::CreateRelayDataStream(FGuid RemoteSessionID)
{
	TUniquePtr<Chaos::VD::FRelayDataStream> NewDataStream = MakeUnique<Chaos::VD::FRelayDataStream>(RemoteSessionID);
	
	return MoveTemp(NewDataStream);
}

void FChaosVDTraceManager::SetPendingExternalRecordingToProcess(const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	TWeakPtr<FChaosVDRecording>& PendingRecording = FChaosVDTraceManagerThreadContext::Get().PendingExternalRecordingWeakPtr;
	ensureMsgf(!PendingRecording.Pin(), TEXT("Attempted to start a secondary trace session before a pending recording instance was processed"));
	PendingRecording = ExistingRecordingPtr;
}

bool FChaosVDTraceManager::ConnectToLiveSession_Internal(uint32 SessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr, const FString& InRequestedSessionName, UE::Trace::FStoreClient::FTraceData&& InTraceDataStream)
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	if (const TSharedPtr<TraceServices::IAnalysisService> TraceAnalysisService = TraceServicesModule.GetAnalysisService())
	{
		// Close this session in case we were already analysing it
		CloseSession(InRequestedSessionName);
		
		SetPendingExternalRecordingToProcess(ExistingRecordingPtr);
	
		if (const TSharedPtr<const TraceServices::IAnalysisSession> NewSession = TraceAnalysisService->StartAnalysis(SessionID, *InRequestedSessionName, MoveTemp(InTraceDataStream)))
		{
			AnalysisSessionByName.Add(InRequestedSessionName, NewSession.ToSharedRef());
			return true;
		}
	}

	return false;
}


FString FChaosVDTraceManager::ConnectToLiveSession(FStringView InSessionHost, uint32 SessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	if (InSessionHost.IsEmpty())
	{
		return FString();
	}

	using namespace UE::Trace;
	FString LiveSessionName = FString::Printf(TEXT("LiveSession[%.*s - %u]"), InSessionHost.Len(), InSessionHost.GetData(), SessionID);

	FStoreClient::FTraceData TraceData = GetTraceDataStreamFromStore(InSessionHost, SessionID);
	if (!TraceData.IsValid())
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to get trace data stream from the trace store | Host [%s] | Session ID [%u]"), __func__, InSessionHost.GetData(), SessionID);
		return FString();
	}

	if (ConnectToLiveSession_Internal(SessionID, ExistingRecordingPtr, LiveSessionName, MoveTemp(TraceData)))
	{
		 return LiveSessionName;
	}

	UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed connect to session | Host [%s] | Session ID [%u]"), __func__, InSessionHost.GetData(), SessionID);

	return FString();
}

TUniquePtr<FArchiveFileWriterGeneric> FChaosVDTraceManager::OpenTraceFileForWrite()
{
	TUniquePtr<FArchiveFileWriterGeneric> FileWriter = nullptr;

#if WITH_CHAOS_VISUAL_DEBUGGER

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FChaosVDRuntimeModule& CVDRuntimeModule = FChaosVDRuntimeModule::Get();
	constexpr int32 MaxAttempts = 10;
	int32 CurrentAttempts = 0;
	while (CurrentAttempts < MaxAttempts)
	{
		FString OutFileName;
		CVDRuntimeModule.GenerateRecordingFileName(OutFileName);
		OutFileName = FPaths::Combine(FPaths::ProfilingDir(), OutFileName);

		if (IFileHandle* FileHandle = PlatformFile.OpenWrite(*OutFileName))
		{
			FileWriter = MakeUnique<FArchiveFileWriterGeneric>(FileHandle, *OutFileName, FileHandle->Tell());
			break;
		}
	
		CurrentAttempts++;

		FPlatformProcess::Sleep(0.1f);
	}

#endif

	return MoveTemp(FileWriter);
}

FString FChaosVDTraceManager::ConnectToLiveSession_Direct(uint16& OutSessionPort, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	TUniquePtr<FArchiveFileWriterGeneric> TraceFileWriter = nullptr;

	if (UChaosVDGeneralSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDGeneralSettings>())
	{
		if (Settings->bSaveMemoryTracesToDisk)
		{
			TraceFileWriter = OpenTraceFileForWrite();
			if (!TraceFileWriter)
			{
				UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to open open file for direct trace stream"), __func__);
				return FString();
			}
		}
	}

	TUniquePtr<UE::Trace::FDirectSocketStream> DirectSocketStream = MakeUnique<UE::Trace::FDirectSocketStream>(MoveTemp(TraceFileWriter));
	OutSessionPort = DirectSocketStream->StartListening();

	if (OutSessionPort == 0)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed to open direct trace stream socket"), __func__);
		return FString();
	}

	FString DirectSessionName = FString::Printf(TEXT("DirectSession[127.0.0.1:%u]"), OutSessionPort);
	
	if (ConnectToLiveSession_Internal(~0u, ExistingRecordingPtr, DirectSessionName, MoveTemp(DirectSocketStream)))
	{
		return DirectSessionName;
	}
	else
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed start trace analysis with direct trace stream socket | [%s]"), __func__, *DirectSessionName);
		return FString();
	}
}

FString FChaosVDTraceManager::ConnectToLiveSession_Relay(FGuid RemoteSessionID, const TSharedPtr<FChaosVDRecording>& ExistingRecordingPtr)
{
	FString RelaySessionName = FString::Printf(TEXT("RelaySession[%s]"), *RemoteSessionID.ToString());

	TUniquePtr<Chaos::VD::FRelayDataStream> RelayDataStream = CreateRelayDataStream(RemoteSessionID);

	if (ConnectToLiveSession_Internal(~0u, ExistingRecordingPtr, RelaySessionName, MoveTemp(RelayDataStream)))
	{
		return RelaySessionName;
	}
	else
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%hs] Failed start trace analysis with relay trace stream | [%s]"), __func__, *RelaySessionName);
		return FString();
	}
}

FString FChaosVDTraceManager::GetLocalTraceStoreDirPath()
{
	UE::Trace::FStoreClient* StoreClient = UE::Trace::FStoreClient::Connect(TEXT("localhost"));

	if (!StoreClient)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to connect to local Trace Store client"), ANSI_TO_TCHAR(__FUNCTION__));
		return TEXT("");
	}

	const UE::Trace::FStoreClient::FStatus* Status = StoreClient->GetStatus();
	if (!Status)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Failed to to get Trace Store status"), ANSI_TO_TCHAR(__FUNCTION__));
		return TEXT("");
	}

	return FString(Status->GetStoreDir());
}

TSharedPtr<const TraceServices::IAnalysisSession> FChaosVDTraceManager::GetSession(const FString& InSessionName)
{
	if (TSharedPtr<const TraceServices::IAnalysisSession>* FoundSession = AnalysisSessionByName.Find(InSessionName))
	{
		return *FoundSession;
	}

	return nullptr;
}

void FChaosVDTraceManager::CloseSession(const FString& InSessionName)
{
	if (const TSharedPtr<const TraceServices::IAnalysisSession>* Session = AnalysisSessionByName.Find(InSessionName))
	{
		if (Session->IsValid())
		{
			(*Session)->Stop(true);
		}

		AnalysisSessionByName.Remove(InSessionName);
	}
}

void FChaosVDTraceManager::StopSession(const FString& InSessionName)
{
	if (const TSharedPtr<const TraceServices::IAnalysisSession>* Session = AnalysisSessionByName.Find(InSessionName))
	{
		if (Session->IsValid())
		{
			(*Session)->Stop(true);
		}
	}
}
