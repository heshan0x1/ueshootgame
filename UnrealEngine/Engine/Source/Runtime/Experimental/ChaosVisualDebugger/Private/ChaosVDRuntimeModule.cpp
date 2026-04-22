// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDRuntimeModule.h"

#include "Modules/ModuleManager.h"

#if WITH_CHAOS_VISUAL_DEBUGGER

#include "Containers/Array.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#include "ChaosVDRecordingDetails.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Trace/RelayTraceWriter.h"

IMPLEMENT_MODULE(FChaosVDRuntimeModule, ChaosVDRuntime);

DEFINE_LOG_CATEGORY(LogChaosVDRuntime);

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

FAutoConsoleCommand ChaosVDStartRecordingCommand(
	TEXT("p.Chaos.StartVDRecording"),
	TEXT("Turn on the recording of debugging data"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UE_AUTORTFM_ONCOMMIT(Args)
		{
			FChaosVDRuntimeModule::Get().StartRecording(Args);
		};
	})
);

FAutoConsoleCommand StopVDStartRecordingCommand(
	TEXT("p.Chaos.StopVDRecording"),
	TEXT("Turn off the recording of debugging data"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UE_AUTORTFM_ONCOMMIT(Args)
		{
			FChaosVDRuntimeModule::Get().StopRecording();
		};
	})
);

static FAutoConsoleVariable CVarChaosVDGTimeBetweenFullCaptures(
	TEXT("p.Chaos.VD.TimeBetweenFullCaptures"),
	10,
	TEXT("Time interval in seconds after which a full capture (not only delta changes) should be recorded"));

static FAutoConsoleVariable CVarChaosVDMaxTimeToWaitForDisconnect(
	TEXT("p.Chaos.VD.MaxTimeToWaitForDisconnectSeconds"),
	5.0f,
	TEXT("Max time to wait after attempting to stop an active trace session. After that time has passed if we are still connected, CVD will continue and eventually error out."));

static FAutoConsoleVariable CVarChaosVDMaxBytesPerRelayChunk(
	TEXT("p.Chaos.VD.MaxBytesPerRelayChunk"),
	65536,
	TEXT("How many bytes per relay data message we should send"));

FChaosVDRecordingStateChangedDelegate FChaosVDRuntimeModule::RecordingStartedDelegate = FChaosVDRecordingStateChangedDelegate();
FChaosVDRecordingStateChangedDelegate FChaosVDRuntimeModule::PostRecordingStartedDelegate = FChaosVDRecordingStateChangedDelegate();
FChaosVDRecordingStateChangedDelegate FChaosVDRuntimeModule::RecordingStopDelegate = FChaosVDRecordingStateChangedDelegate();
FChaosVDRecordingStartFailedDelegate FChaosVDRuntimeModule::RecordingStartFailedDelegate = FChaosVDRecordingStartFailedDelegate();
FChaosVDCaptureRequestDelegate FChaosVDRuntimeModule::PerformFullCaptureDelegate = FChaosVDCaptureRequestDelegate();
FTransactionallySafeRWLock FChaosVDRuntimeModule::DelegatesRWLock = FTransactionallySafeRWLock();

FChaosVDRuntimeModule& FChaosVDRuntimeModule::Get()
{
	return FModuleManager::Get().LoadModuleChecked<FChaosVDRuntimeModule>(TEXT("ChaosVDRuntime"));
}

bool FChaosVDRuntimeModule::IsLoaded()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("ChaosVDRuntime"));
}

void FChaosVDRuntimeModule::StartupModule()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("StartCVDRecording")))
	{
		TArray<FString, TInlineAllocator<1>> CVDOptions;

		{
			FString CVDHostAddress;
			if (FParse::Value(FCommandLine::Get(), TEXT("CVDHost="), CVDHostAddress))
			{
				CVDOptions.Emplace(TEXT("Server"));
				CVDOptions.Emplace(MoveTemp(CVDHostAddress));
			}
		}
        
        StartRecording(CVDOptions);
	}
	else
	{
		
#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED
		UE::Trace::ToggleChannel(TEXT("ChaosVDChannel"), false);
#endif

	}
}

void FChaosVDRuntimeModule::ShutdownModule()
{
	if (bIsRecording)
	{
		StopRecording();
	}

	FTraceAuxiliary::OnTraceStopped.RemoveAll(this);
}

void FChaosVDRuntimeModule::StartRecording(TConstArrayView<FString> Args)
{
	FChaosVDStartRecordingCommandMessage StartRecordingCommand;
	if (Args.Num() == 0 || Args[0] == TEXT("File"))
	{
		StartRecordingCommand.RecordingMode = EChaosVDRecordingMode::File;
		StartRecordingCommand.TransportMode = EChaosVDTransportMode::FileSystem;
	}
	else if(Args[0] == TEXT("Server"))
	{
		StartRecordingCommand.RecordingMode = EChaosVDRecordingMode::Live;
		StartRecordingCommand.TransportMode = EChaosVDTransportMode::TraceServer;
		StartRecordingCommand.Target = Args.IsValidIndex(1) ? Args[1] : TEXT("127.0.0.1");
	}

	StartRecording(StartRecordingCommand);
}

int32 FChaosVDRuntimeModule::GenerateUniqueID()
{
	int32 NewID = 0;
	UE_AUTORTFM_OPEN
	{
		NewID = LastGeneratedID++;
	};

	return NewID;
}

FString FChaosVDRuntimeModule::GetLastRecordingFileNamePath() const
{
	return FString();
}

FChaosVDTraceDetails FChaosVDRuntimeModule::GetCurrentTraceSessionDetails() const
{
	FChaosVDTraceDetails Details;
	if (ExternalTraceStatusDelegate.IsBound())
	{
		return ExternalTraceStatusDelegate.Execute();
	}
	
	if (FTraceAuxiliary::IsConnected(Details.SessionGuid, Details.TraceGuid))
	{
		Details.TraceTarget = FTraceAuxiliary::GetTraceDestinationString();
		Details.Mode = GetCurrentRecordingMode();
		Details.TransportMode = CurrentTransportMode;
		Details.MarkAsValid();
		return Details;
	}

	return Details;
}

void FChaosVDRuntimeModule::StopTrace()
{
	bRequestedStop = true;
	FTraceAuxiliary::Stop();
}

void FChaosVDRuntimeModule::GenerateRecordingFileName(FString& OutFileName)
{
	FStringFormatOrderedArguments NameArgs { FString(FApp::GetProjectName()), LexToString(FApp::GetBuildTargetType()), FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) };
	
	OutFileName = FString::Format(TEXT("ChaosVD-{0}-{1}-{2}.utrace"), NameArgs);
}

void FChaosVDRuntimeModule::RegisterExternalRelayExecutor(const FChaosVDRelayTraceDataDelegate& InExternalRelayExecutor)
{
	if (!ensureMsgf(!ExternalRelayExecutorDelegate.IsBound(), TEXT("An external relay executor is already registered!")))
	{
		return;
	}

	ExternalRelayExecutorDelegate = InExternalRelayExecutor;
}

void FChaosVDRuntimeModule::UnregisterCurrentExternalRelayExecutor()
{
	ExternalRelayExecutorDelegate.Unbind();
}

void FChaosVDRuntimeModule::RegisterExternalTraceStatusProvider(const FChaosVDExternalTraceStatusDelegate& InExternalTraceStatusCallback)
{
	ExternalTraceStatusDelegate = InExternalTraceStatusCallback;
}

void FChaosVDRuntimeModule::UnregisterExternalTraceStatusProvider()
{
	ExternalTraceStatusDelegate.Unbind();
}

void FChaosVDRuntimeModule::RelayTraceData()
{
	if (ensure(ExternalRelayExecutorDelegate.IsBound() && RelayWriter.IsValid()))
	{
		ExternalRelayExecutorDelegate.Execute(*RelayWriter);
	}
}

void FChaosVDRuntimeModule::SetupRelayDataPumpDelegates()
{
	if (!ensure(RelayWriter))
	{
		return;
	}

	RelayWriter->OnNewDataAvailable().BindRaw(this, &FChaosVDRuntimeModule::RelayTraceData);
}

void FChaosVDRuntimeModule::ClearRelayDataPumpDelegates()
{
	if (RelayWriter)
	{
		RelayWriter->OnNewDataAvailable().Unbind();
	}
}

bool FChaosVDRuntimeModule::RequestFullCapture(float DeltaTime)
{
	// Full capture intervals are clamped to be no lower than 1 sec
	UE::TReadScopeLock ReadLock (DelegatesRWLock);
	PerformFullCaptureDelegate.Broadcast(EChaosVDFullCaptureFlags::Particles);
	return true;
}

bool FChaosVDRuntimeModule::RecordingTimerTick(float DeltaTime)
{
	if (bIsRecording)
	{
		AccumulatedRecordingTime += DeltaTime;
	}
	
	return true;
}

void FChaosVDRuntimeModule::HandleTraceConnectionEstablished()
{
	UE_LOG(LogChaosVDRuntime, Log, TEXT("Trace connection established."));

	if (OnConnectionDelegateHandle.IsValid())
	{
		FTraceAuxiliary::OnConnection.RemoveAll(this);
		OnConnectionDelegateHandle.Reset();
	}

	if (!ensureAlwaysMsgf(bIsRecording, TEXT("Received a trace connection established callback but no CVD trace is active. This should not happen.")))
	{
		return;
	}

	{
		UE::TReadScopeLock ReadLock(DelegatesRWLock);
		RecordingStartedDelegate.Broadcast();
	}

	TraceConnectionDetailsUpdatedDelegate.Broadcast();
	
	constexpr int32 MinAllowedTimeInSecondsBetweenCaptures = 1;
	int32 ConfiguredTimeBetweenCaptures = CVarChaosVDGTimeBetweenFullCaptures->GetInt();

	ensureAlwaysMsgf(ConfiguredTimeBetweenCaptures > MinAllowedTimeInSecondsBetweenCaptures,
	                 TEXT("The minimum allowed time interval between full captures is [%d] seconds, but [%d] seconds were configured. Clamping to [%d] seconds"),
	                 MinAllowedTimeInSecondsBetweenCaptures, ConfiguredTimeBetweenCaptures, MinAllowedTimeInSecondsBetweenCaptures);

	FullCaptureRequesterHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FChaosVDRuntimeModule::RequestFullCapture),
	                                                                  FMath::Clamp(ConfiguredTimeBetweenCaptures, MinAllowedTimeInSecondsBetweenCaptures, TNumericLimits<int32>::Max()));

	RecordingTimerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FChaosVDRuntimeModule::RecordingTimerTick));

	{
		UE::TReadScopeLock ReadLock(DelegatesRWLock);
		PostRecordingStartedDelegate.Broadcast();
	}
}


void FChaosVDRuntimeModule::StartRecording(const FChaosVDStartRecordingCommandMessage& InRecordingStartCommand)
{
	if (!ensureAlwaysMsgf(!bIsRecording, TEXT("Received a recording start command while a recording is already active")))
	{
		UE_LOG(LogChaosVDRuntime, Log, TEXT("[%hs] There is an active CVD recording. Attempting to stop it..."), __func__)
		StopRecording();
	}

	// Start with a generic Failure reason
	FText FailureReason = LOCTEXT("SeeLogsForErrorDetailsText","Please see the logs for more details...");

#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED

	// Other tools could bee using trace
	// This is aggressive but until Trace supports multi-sessions, just take over.
	if (FTraceAuxiliary::IsConnected())
	{
		UE_LOG(LogChaosVDRuntime, Log, TEXT("[%s] There is an active trace session. attempting to disconnect..."), ANSI_TO_TCHAR(__FUNCTION__));

		//TODO: We should make the wait async like we do whe we attempt to connect to a live session
		if (FTraceAuxiliary::Stop() && WaitForTraceSessionDisconnect())
		{
			UE_LOG(LogChaosVDRuntime, Log, TEXT("[%s] Successful disconnect attempt!."), ANSI_TO_TCHAR(__FUNCTION__));
		}
		else
		{
			FailureReason = LOCTEXT("FailedToStopActiveRecordingErrorMessage", "Failed to Stop active Trace Session.");
		}
	}

	SaveAndDisabledCurrentEnabledTraceChannels();

	EnableRequiredTraceChannels();

	FTraceAuxiliary::FOptions TracingOptions;
	TracingOptions.bExcludeTail = true;

	if (InRecordingStartCommand.RecordingMode == EChaosVDRecordingMode::File)
	{
		FString FileTarget;
		GenerateRecordingFileName(FileTarget);

		UE_LOG(LogChaosVDRuntime, Log, TEXT("[%hs] Generated trace file name [%s]"), __func__, *FileTarget);

		switch (InRecordingStartCommand.TransportMode)
		{
		case EChaosVDTransportMode::FileSystem:
			bIsRecording = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *FileTarget, nullptr, &TracingOptions);
			break;
		case EChaosVDTransportMode::TraceServer:
			bIsRecording = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, *InRecordingStartCommand.Target, nullptr, &TracingOptions);
			break;
		case EChaosVDTransportMode::Direct:
		case EChaosVDTransportMode::Relay:
		case EChaosVDTransportMode::Invalid:
			UE_LOG(LogChaosVDRuntime, Error, TEXT("[%hs] Unsupported Transport mode [%s]"), __func__, *UEnum::GetValueAsString(InRecordingStartCommand.TransportMode))
			break;
		}
		
		CurrentRecordingMode = EChaosVDRecordingMode::File;
	}
	else if(InRecordingStartCommand.RecordingMode == EChaosVDRecordingMode::Live)
	{
		switch (InRecordingStartCommand.TransportMode)
		{
		case EChaosVDTransportMode::Direct:
		case EChaosVDTransportMode::TraceServer:
			bIsRecording = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::Network, *InRecordingStartCommand.Target, nullptr, &TracingOptions);
			break;
		case EChaosVDTransportMode::Relay:
			{
				if (!ensureMsgf(ExternalRelayExecutorDelegate.IsBound(), TEXT("Cannot start a trace in relay mode without a Relay Executor")))
				{
					UE_LOG(LogChaosVDRuntime, Error, TEXT("[%hs] Cannot start a trace in relay mode without a Relay Executor"), __func__);
					break;
				}
				
				if (RelayWriter && !RelayWriter->IsClosed())
				{
					UE_LOG(LogChaosVDRuntime, Warning, TEXT("[%hs] Attempting to relay trace, but there is a relay writer still open"), __func__);
					RelayWriter->Close();
				}

				RelayWriter = MakeUnique<Chaos::VD::FRelayTraceWriter>();
				RelayWriter->SetMaxBytesPerBunch(CVarChaosVDMaxBytesPerRelayChunk->GetInt());
				
				bIsRecording = FTraceAuxiliary::Relay(reinterpret_cast<UPTRINT>(RelayWriter.Get()), Chaos::VD::FRelayTraceWriter::WriteHelper, Chaos::VD::FRelayTraceWriter::CloseHelper, nullptr, &TracingOptions);

				if (bIsRecording)
				{
					SetupRelayDataPumpDelegates();
				}

				break;
			}
		case EChaosVDTransportMode::Invalid:
		case EChaosVDTransportMode::FileSystem:
		UE_LOG(LogChaosVDRuntime, Error, TEXT("[%hs] Unsupported Transport mode [%s]"), __func__, *UEnum::GetValueAsString(InRecordingStartCommand.TransportMode))
			break;
		}

		CurrentRecordingMode = EChaosVDRecordingMode::Live;
	}
	else
	{
		FailureReason = LOCTEXT("WrongCommandArgumentsError", "The start recording command was called with invalid arguments");
	}
#endif
	
	AccumulatedRecordingTime = 0.0f;

	if (ensureMsgf(bIsRecording, TEXT("Failed to start CVD recording | Reason [%s]"), *FailureReason.ToString()))
	{
		CurrentTransportMode = InRecordingStartCommand.TransportMode;

		if (ensureMsgf(!OnConnectionDelegateHandle.IsValid(), TEXT("Starting a trace while we are waiting for a pending connection")))
		{
			OnConnectionDelegateHandle = FTraceAuxiliary::OnConnection.AddRaw(this, &FChaosVDRuntimeModule::HandleTraceConnectionEstablished);
		}

		// Start Listening for Trace Stopped events, in case Trace is stopped outside our control so we can gracefully stop CVD recording and log a warning
		if (ensureMsgf(!OnTraceStoppedDelegateHandle.IsValid(), TEXT("Starting a trace while we are waiting for a trace session to stop.")))
		{
			OnTraceStoppedDelegateHandle = FTraceAuxiliary::OnTraceStopped.AddRaw(this, &FChaosVDRuntimeModule::HandleTraceStopRequest);
		}
	}
	else
	{
		UE_LOG(LogChaosVDRuntime, Error, TEXT("[%s] Failed to start CVD recording | Reason: [%s]"), ANSI_TO_TCHAR(__FUNCTION__), *FailureReason.ToString());

#if WITH_EDITOR
		FMessageDialog::Open(EAppMsgType::Ok, FText::FormatOrdered(LOCTEXT("StartRecordingFailedMessage", "Failed to start CVD recording. \n\n{0}"), FailureReason));
#endif

		{
			UE::TReadScopeLock ReadLock(DelegatesRWLock);
			RecordingStartFailedDelegate.Broadcast(FailureReason);
		}

		CurrentRecordingMode = EChaosVDRecordingMode::Invalid;

		RelayWriter = nullptr;
	}
}

void FChaosVDRuntimeModule::StopRecording()
{
	FTraceAuxiliary::OnConnection.RemoveAll(this);
	OnConnectionDelegateHandle = FDelegateHandle();

	FTraceAuxiliary::OnTraceStopped.RemoveAll(this);
	OnTraceStoppedDelegateHandle = FDelegateHandle();

	CurrentRecordingMode = EChaosVDRecordingMode::Invalid;
	CurrentTransportMode = EChaosVDTransportMode::Invalid;

	if (!bIsRecording)
	{
		UE_LOG(LogChaosVDRuntime, Warning, TEXT("[%s] Attempted to stop recorded when there is no CVD recording active."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED
	RestoreTraceChannelsToPreRecordingState();

	StopTrace();
#endif

	if (FullCaptureRequesterHandle.IsValid())
	{
		FTSTicker::RemoveTicker(FullCaptureRequesterHandle);

		FullCaptureRequesterHandle.Reset();
	}
	
	if (RecordingTimerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(RecordingTimerHandle);
		RecordingTimerHandle.Reset();
	}

	ClearRelayDataPumpDelegates();

	if (RelayWriter)
	{
		RelayWriter->Close();
	}
	
	bIsRecording = false;
	AccumulatedRecordingTime = 0.0f;

	{
		UE::TReadScopeLock ReadLock(DelegatesRWLock);
		RecordingStopDelegate.Broadcast();
	}
}

void FChaosVDRuntimeModule::HandleTraceStopRequest(FTraceAuxiliary::EConnectionType TraceType, const FString& TraceDestination)
{
	if (bIsRecording)
	{
		if (!ensure(bRequestedStop))
		{
			UE_LOG(LogChaosVDRuntime, Warning, TEXT("Trace Recording has been stopped unexpectedly"));

#if WITH_EDITOR
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("UnexpectedStopMessage", "Trace recording has been stopped unexpectedly. CVD cannot continue with the recording session... "));
#endif
		}

		StopRecording();
	}

	bRequestedStop = false;
}

bool FChaosVDRuntimeModule::WaitForTraceSessionDisconnect()
{
	float MaxWaitTime = CVarChaosVDMaxTimeToWaitForDisconnect->GetFloat();
	float CurrentWaitTime = 0.0f;

#if WITH_EDITOR
	FScopedSlowTask DisconnectAttemptSlowTask(MaxWaitTime, LOCTEXT("DisconnectAttemptMessage", " Active Trace Session detected, attempting to disconnect ..."));

	constexpr bool bShowCancelButton = false;
	constexpr bool bAllowInPIE = true;
	DisconnectAttemptSlowTask.MakeDialog(bShowCancelButton, bAllowInPIE);
#endif

	while (CurrentWaitTime < MaxWaitTime)
	{
		constexpr float WaitInterval = 0.1f;
		FPlatformProcess::Sleep(0.1f);

		if (!FTraceAuxiliary::IsConnected())
		{
			return true;
		}

		// We don't need to be precise for this, we can just accumulate the wait
		CurrentWaitTime += WaitInterval;

#if WITH_EDITOR
		DisconnectAttemptSlowTask.EnterProgressFrame(CurrentWaitTime);
#endif
	}

	return FTraceAuxiliary::IsConnected();
}

void FChaosVDRuntimeModule::SaveAndDisabledCurrentEnabledTraceChannels()
{
	// Until we support allowing other channels, indicate in the logs that we are disabling everything else
	UE_LOG(LogChaosVDRuntime, Log, TEXT("[%s] Disabling additional trace channels..."), ANSI_TO_TCHAR(__FUNCTION__));

#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED
	OriginalTraceChannelsState.Reset();

	// Disable any enabled additional channel
	UE::Trace::EnumerateChannels([](const ANSICHAR* ChannelName, bool bEnabled, void* SavedTraceChannelsPtr)
	{
		TMap<FString, bool>* SavedTraceChannels = static_cast<TMap<FString, bool>*>(SavedTraceChannelsPtr);
		FString ChannelNameFString(ChannelName);
		SavedTraceChannels->Add(ChannelNameFString, bEnabled);
		if (bEnabled)
		{
			UE::Trace::ToggleChannel(ChannelNameFString.GetCharArray().GetData(), false);
		}
	}
	, &OriginalTraceChannelsState);
#endif
}

void FChaosVDRuntimeModule::RestoreTraceChannelsToPreRecordingState()
{
#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED
	UE_LOG(LogChaosVDRuntime, Log, TEXT("[%s] Restoring trace channels state..."), ANSI_TO_TCHAR(__FUNCTION__));

	for (const TPair<FString, bool>& ChannelWithState : OriginalTraceChannelsState)
	{
		UE::Trace::ToggleChannel(GetData(ChannelWithState.Key), ChannelWithState.Value); 
	}
	
	OriginalTraceChannelsState.Reset();
#endif
}

void FChaosVDRuntimeModule::EnableRequiredTraceChannels()
{
#if UE_TRACE_ENABLED || UE_TRACE_MINIMAL_ENABLED
	UE::Trace::ToggleChannel(TEXT("ChaosVDChannel"), true); 
	UE::Trace::ToggleChannel(TEXT("Frame"), true);
#endif

#if UE_TRACE_ENABLED
	UE::Trace::ToggleChannel(TEXT("Log"), true);
#endif
}

#undef LOCTEXT_NAMESPACE
#else

IMPLEMENT_MODULE(FDefaultModuleImpl, ChaosVDRuntime);

#endif
