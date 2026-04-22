// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestControllerBase.h"

#include "AutomatedPerfTesting.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/LocalPlayer.h"
#include "AutomatedPerfTestInterface.h"
#include "AutomatedPerfTestProjectSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/TaskGraphInterfaces.h"
#include "VideoRecordingSystem.h"
#include "PlatformFeatures.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceScreenshot.h"
#include "UnrealClient.h"
#include "UnrealEngine.h"
#include "GauntletModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AutomatedPerfTestControllerBase)

DEFINE_LOG_CATEGORY(LogAutomatedPerfTest)
CSV_DEFINE_CATEGORY(AutomatedPerfTest, true);

static float GAPTDynamicResLockedScreenPercentage = 100.f;
static FAutoConsoleVariableRef CVarDynamicResLockedScreenPercentage(
	TEXT("APT.DynamicRes.LockedScreenPercentage"),
	GAPTDynamicResLockedScreenPercentage,
	TEXT("Target resolution percentage, configurable per platform. Use -AutomatedPerfTest.LockDynamicRes to force the resolution to this"),
	ECVF_Default
);

void UAutomatedPerfTestControllerBase::OnPreWorldInitializeInternal(UWorld* World, const UWorld::InitializationValues IVS)
{
	TryEarlyExec(World);
	OnPreWorldInitialize(World);

	if (RequestsLockedDynRes())
	{
		IConsoleVariable* CVarTestScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.dynamicres.testscreenpercentage"));
		if (ensure(CVarTestScreenPercentage))
		{
			if (CVarTestScreenPercentage->GetFloat() != GAPTDynamicResLockedScreenPercentage)
			{
				UE_LOG(LogAutomatedPerfTest, Display, TEXT("Locking screen percentage to %.2f"), GAPTDynamicResLockedScreenPercentage);
				CVarTestScreenPercentage->Set(GAPTDynamicResLockedScreenPercentage);
			}
		}
	}
}

void UAutomatedPerfTestControllerBase::OnPreWorldInitialize(UWorld* World)
{
	check(World);
	World->GameStateSetEvent.AddUObject(this, &ThisClass::OnGameStateSet);
	World->OnWorldBeginPlay.AddUObject(this, &ThisClass::OnWorldBeginPlay);
}

void UAutomatedPerfTestControllerBase::TryEarlyExec(UWorld* const World)
{
	check(World);
	
	if (GEngine)
	{
		// Search the list of deferred commands
		const TArray<FString>& DeferredCmds = GEngine->DeferredCommands;
		TArray<int32> ExecutedIndices;
		for (int32 DeferredCmdIndex = 0; DeferredCmdIndex < DeferredCmds.Num(); ++DeferredCmdIndex)
		{
			// If the deferred command is one that should be executed early
			const FString& DeferredCmd = DeferredCmds[DeferredCmdIndex];
			if (CmdsToExecEarly.ContainsByPredicate([&DeferredCmd](const FString& CmdToFind) { return DeferredCmd.StartsWith(CmdToFind); }))
			{
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("EarlyExec: executing '%s' early."), *DeferredCmd);
				GEngine->Exec(World, *DeferredCmd);
				ExecutedIndices.Push(DeferredCmdIndex);
			}
		}

		// Remove the executed commands from the list of deferred commands
		// Note: This is done in reverse order to ensure the cached indices remain valid
		while (!ExecutedIndices.IsEmpty())
		{
			GEngine->DeferredCommands.RemoveAt(ExecutedIndices.Pop());
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted EarlyExec without GEngine being ready"))
	}
}

void UAutomatedPerfTestControllerBase::OnWorldBeginPlay()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("OnWorldBeginPlay"));
	SetupTest();
}

void UAutomatedPerfTestControllerBase::OnGameStateSet(AGameStateBase* const GameStateBase)
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Game State Set"));
	if (UWorld* const World = GetWorld())
	{
		World->GameStateSetEvent.RemoveAll(this);
	}
}

UAutomatedPerfTestControllerBase::UAutomatedPerfTestControllerBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TraceChannels("default,screenshot,stats")
	, bRequestsFPSChart(false)
	, bRequestsInsightsTrace(false)
	, bRequestsCSVProfiler(false)
	, bRequestsVideoCapture(false)
	, bRequestsLockedDynRes(false)
	, InsightsRegionID(0)
	, ArtifactOutputPath()
    , CSVOutputMode(EAutomatedPerfTestCSVOutputMode::Single)
{
	// cache this off once, so that it's consistent throughout a session
	TestDatetime = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
}

FString UAutomatedPerfTestControllerBase::GetTestName()
{
	return TestID;
}

FString UAutomatedPerfTestControllerBase::GetDeviceProfile()
{
	return DeviceProfileOverride.IsEmpty() ? UDeviceProfileManager::Get().GetActiveDeviceProfileName() : DeviceProfileOverride;  
}

FString UAutomatedPerfTestControllerBase::GetTestID()
{
	const TArray<FString> TestCaseIDElements = {FApp::GetBuildVersion(),
										  FPlatformProperties::PlatformName(),
										  TestDatetime,
								          *GetDeviceProfile(),
								          *GetTestName()};

	// TODO make this format definable in project settings, similar to how MovieRenderQueue does it for renders
	// construct a unique ID of the form BuildVersion_PlatformName_YYYYMMDD-HHMMSS_DeviceProfile_TestName
	FString TestCaseID = FString::Join(TestCaseIDElements, TEXT("_"));

	return TestCaseID;
}

FString UAutomatedPerfTestControllerBase::GetOverallRegionName()
{
	return GetTestID() + "_" + "Overall";
}

FString UAutomatedPerfTestControllerBase::GetTraceChannels()
{
	return TraceChannels;
}

bool UAutomatedPerfTestControllerBase::RequestsInsightsTrace() const
{
	return bRequestsInsightsTrace;
}

bool UAutomatedPerfTestControllerBase::RequestsCSVProfiler() const
{
	return bRequestsCSVProfiler;
}

bool UAutomatedPerfTestControllerBase::RequestsFPSChart() const
{
	return bRequestsFPSChart;
}

bool UAutomatedPerfTestControllerBase::RequestsVideoCapture() const
{
	return bRequestsVideoCapture;
}

bool UAutomatedPerfTestControllerBase::RequestsLockedDynRes() const
{
	return bRequestsLockedDynRes;
}

bool UAutomatedPerfTestControllerBase::TryStartInsightsTrace()
{
	const FString TraceFileName = GetInsightsFilename() + ".utrace";
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Attempting to start insights trace to file %s with channels %s"), *TraceFileName, *GetTraceChannels());
	return FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TraceFileName, *GetTraceChannels());
}

bool UAutomatedPerfTestControllerBase::TryStopInsightsTrace()
{
	if(FTraceAuxiliary::IsConnected())
	{
		return FTraceAuxiliary::Stop();
	}
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStartCSVProfiler()
{
	const FString DestinationFolder = ArtifactOutputPath.IsEmpty() ? FString() : FPaths::Combine(ArtifactOutputPath, "CSV");
	return TryStartCSVProfiler(GetCSVFilename(), DestinationFolder);
}

bool UAutomatedPerfTestControllerBase::TryStartCSVProfiler(FString CSVFileName, const FString& DestinationFolder, int32 Frames)
{
#if CSV_PROFILER
	if(FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		if(!CSVFileName.EndsWith(".csv"))
		{
			CSVFileName += TEXT(".csv");
		}

		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Attempting to start CSV Profile to file %s"), *CSVFileName);

		CsvProfiler->SetMetadata(TEXT("TestID"), *TestID);
		CsvProfiler->SetMetadata(TEXT("Datetime"), *TestDatetime);
		CsvProfiler->SetMetadata(TEXT("ResX"), *FString::FromInt(GSystemResolution.ResX));
		CsvProfiler->SetMetadata(TEXT("ResY"), *FString::FromInt(GSystemResolution.ResY));

		UE_LOG(LogGauntlet, Log, TEXT("CSV Profiler Destination Filename: %s, Destination Folder: %s"), 
				*CSVFileName, DestinationFolder.IsEmpty()? TEXT("<Default>") : *DestinationFolder);
		
		CsvProfiler->BeginCapture(Frames, DestinationFolder, CSVFileName);
		CsvProfiler->SetDeviceProfileName(GetDeviceProfile());
		
		return CsvProfiler->IsCapturing();
	}
#endif
	UE_LOG(LogAutomatedPerfTest, Warning, TEXT("CSVProfiler Start requested, but not available."))
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStopCSVProfiler()
{
#if CSV_PROFILER
	if(FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		const bool bSafeToStopCsvProfiler = CsvProfiler->IsCapturing() && !CsvProfiler->IsEndCapturePending();
		if (bSafeToStopCsvProfiler)
		{
			const FGraphEventRef AutomatedPerfTestEndEvent = FGraphEvent::CreateGraphEvent();
			CsvProfiler->EndCapture(AutomatedPerfTestEndEvent);
		}
		return true;
	}
#endif
	UE_LOG(LogAutomatedPerfTest, Warning, TEXT("CSVProfiler Stop requested, but not available."))
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStartFPSChart()
{
	// don't open the folder the FPS chart gets sent to on exit, as it can cause issues when running unattended
	GEngine->Exec(GetWorld(), TEXT("t.FPSChart.OpenFolderOnDump 0"));
	GEngine->StartFPSChart(*GetOverallRegionName(), false);

	return true;
}

bool UAutomatedPerfTestControllerBase::TryStopFPSChart()
{
	GEngine->StopFPSChart(*GetOverallRegionName());
	return true;
}

bool UAutomatedPerfTestControllerBase::TryStartVideoCapture()
{
	if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
	{
		const EVideoRecordingState RecordingState = VideoRecordingSystem->GetRecordingState();

		if (RecordingState == EVideoRecordingState::None)
		{
			VideoRecordingSystem->EnableRecording(true);
			
			VideoRecordingTitle = FText::FromString(FPaths::Combine(FPaths::ProjectSavedDir(), GetTestID()));
			const FVideoRecordingParameters VideoRecordingParameters(VideoRecordingSystem->GetMaximumRecordingSeconds(), true, false, false, FPlatformMisc::GetPlatformUserForUserIndex(0));
			VideoRecordingSystem->NewRecording(*GetTestID(), VideoRecordingParameters);

			if (VideoRecordingSystem->IsEnabled())
			{
				if (VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Starting || VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Recording)
				{
					UE_LOG(LogAutomatedPerfTest, Log, TEXT("Starting video recording %s..."), *GetTestID());
					return true;
				}
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Failed to start video recording %s. Current state is %i"), *GetTestID(), VideoRecordingSystem->GetRecordingState());
			}
			else
			{
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording could not be enabled."));
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Could not start a new recording, may be already recording."));
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording system is null."));
	}

	return false;	
}

bool UAutomatedPerfTestControllerBase::TryFinalizingVideoCapture(const bool bStopAutoContinue/*=false*/)
{
	if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
	{
		if (VideoRecordingSystem->GetRecordingState() != EVideoRecordingState::None)
		{
			VideoRecordingSystem->FinalizeRecording(true, VideoRecordingTitle, FText::GetEmpty(), bStopAutoContinue);

			if (VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Finalizing)
			{
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("Finalizing recording..."));
				VideoRecordingSystem->GetOnVideoRecordingFinalizedDelegate().AddUObject(this, &ThisClass::OnVideoRecordingFinalized);
				return true;
			}
			else
			{
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize video recording, but current state %i is not %i"), VideoRecordingSystem->GetRecordingState(), EVideoRecordingState::Finalizing)
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize video recording, but state is %i"), VideoRecordingSystem->GetRecordingState())
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording system is null."));
	}

	return false;	
}

void UAutomatedPerfTestControllerBase::SetupTest()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: SetupTest"));

	SetupGameModeInstance();

	// Subclasses should implement their own transitions from SetupTest to RunTest depending on their needs

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSV Output Mode: %s"), *UEnum::GetValueAsString(CSVOutputMode))
	UE_LOG(LogGauntlet, Log, TEXT("Setup Test: %s"), *GetTestID());
}

void UAutomatedPerfTestControllerBase::RunTest()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: RunTest"));

	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_RunTest(GameMode);
	}

	UE_LOG(LogGauntlet, Log, TEXT("Running Test: %s"), *GetTestID());
}

void UAutomatedPerfTestControllerBase::TeardownTest(bool bExitAfterTeardown)
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: TeardownTest"));

	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_TeardownTest(GameMode);
	}

	if(bExitAfterTeardown)
	{
		TriggerExitAfterDelay();
	}

	UE_LOG(LogGauntlet, Log, TEXT("Teardown Test: %s"), *GetTestID());
}

void UAutomatedPerfTestControllerBase::TriggerExitAfterDelay()
{
	const float Delay = GetDefault<UAutomatedPerfTestProjectSettings>()->TeardownToExitDelay;

	// Gauntlet test controllers are UObject types.
	TObjectPtr<UAutomatedPerfTestControllerBase> ControllerInstance(this);
	FTSTicker::GetCoreTicker().AddTicker(TEXT("ReplayComplete"), Delay, [ControllerInstance](float)
	{
		// Capturing this here is safe as this controller is only destroyed after 
		// Exit is called below and the Gauntlet test ends. When running Gauntlet 
		// tests, the controller instances are owned by the Gauntlet Module 
		if(ControllerInstance)
		{
			ControllerInstance->Exit();
		}
		return false;
	});
}

void UAutomatedPerfTestControllerBase::Exit()
{
	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_Exit(GameMode);
	}

	if(RequestsCSVProfiler())
	{
#if CSV_PROFILER
		if(FCsvProfiler::Get()->IsWritingFile())
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSVProfile requested, and test is exiting, but CSV Profiler isn't done writing."))
			// if we requested a CSV Profile, and the CSV Profiler is still writing the file, add a lambda to call the EndAutomatedPerfTest function
			// so that we don't exit out of the application before the CSV Profiler is done
			
			// TODO investigate if this is necessary if we set csv.BlockOnCaptureEnd
			FCsvProfiler::Get()->OnCSVProfileFinished().AddLambda([this](const FString& Filename)
			{
				EndAutomatedPerfTest();
			});
			return;
		}
#endif
	}
	
	EndAutomatedPerfTest();
}

AGameModeBase* UAutomatedPerfTestControllerBase::GetGameMode() const
{
	return GameMode;
}

void UAutomatedPerfTestControllerBase::TakeScreenshot(FString ScreenshotName)
{
	if(RequestsInsightsTrace())
	{
		// trace screenshots are disabled in shipping by default
#if UE_SCREENSHOT_TRACE_ENABLED
		FTraceScreenshot::RequestScreenshot(ScreenshotName, false, LogAutomatedPerfTest);
#endif
	}
	else
	{
		FScreenshotRequest::RequestScreenshot(ScreenshotName, false, false);
	}
}

void UAutomatedPerfTestControllerBase::SetupGameModeInstance()
{
	GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : NULL;

	if (GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_SetupTest(GameMode);
	}
}

APlayerController* UAutomatedPerfTestControllerBase::GetPlayerController()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (ULocalPlayer* Player = GEngine->GetLocalPlayerFromControllerId(World, 0))
	{
		FLocalPlayerContext Context(Player);
		if (APlayerController* Controller = Context.GetPlayerController())
		{
			return Controller;
		}
	}

	return nullptr;
}

void UAutomatedPerfTestControllerBase::SetupProfiling()
{

	if (RequestsCSVProfiler() && CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Single)
	{
		TryStartCSVProfiler();
	}

	if (RequestsFPSChart())
	{
		TryStartFPSChart();
	}

	if (RequestsVideoCapture())
	{
		TryStartVideoCapture();
	}
}

void UAutomatedPerfTestControllerBase::InitializeInsights()
{
	if (RequestsInsightsTrace())
	{
		TryStartInsightsTrace();
	}
}

void UAutomatedPerfTestControllerBase::ShutdownInsights()
{
	if (RequestsInsightsTrace())
	{
		TryStopInsightsTrace();
	}
}

void UAutomatedPerfTestControllerBase::MarkProfilingStart()
{
	if (RequestsInsightsTrace())
	{
		InsightsRegionID = TRACE_BEGIN_REGION_WITH_ID(*GetOverallRegionName(), TEXT("AutomatedPerfTest"));
	}

	if (RequestsCSVProfiler())
	{
		if (CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Separate)
		{
			TryStartCSVProfiler();
		}
		CSV_EVENT(AutomatedPerfTest, TEXT("START"), *GetOverallRegionName())
	}
}

void UAutomatedPerfTestControllerBase::TeardownProfiling()
{
	if (RequestsCSVProfiler())
	{
		CSV_EVENT(AutomatedPerfTest, TEXT("END"), *GetOverallRegionName())
		if (CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Single)
		{
			TryStopCSVProfiler();
		}
	}

	if (RequestsFPSChart())
	{
		TryStopFPSChart();
	}

	if (RequestsVideoCapture())
	{
		if (!TryFinalizingVideoCapture())
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize requested video capture, but failed."))
		}
	}
}

void UAutomatedPerfTestControllerBase::MarkProfilingEnd()
{
	if (RequestsInsightsTrace())
	{
		if (InsightsRegionID != 0)
		{
			TRACE_END_REGION_WITH_ID(InsightsRegionID);
		}
	}

	if (RequestsCSVProfiler())
	{
		CSV_EVENT(AutomatedPerfTest, TEXT("END"), *GetOverallRegionName())
		if (CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Separate)
		{
			TryStopCSVProfiler();
		}
	}
}

FString UAutomatedPerfTestControllerBase::GetInsightsFilename()
{
	return GetTestID();
}

FString UAutomatedPerfTestControllerBase::GetCSVFilename()
{
	return GetTestID();
}

void UAutomatedPerfTestControllerBase::SetCSVOutputMode(EAutomatedPerfTestCSVOutputMode NewOutputMode)
{
	CSVOutputMode = NewOutputMode;	
}

void UAutomatedPerfTestControllerBase::OnInit()
{
	Super::OnInit();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: OnInit"));

	// don't stop on separator because this will come in comma-separated
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.TraceChannels="), TraceChannels, false);
	
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.DeviceProfileOverride="), DeviceProfileOverride);
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.TestID="), TestID);
	
	if (FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoInsightsTrace")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Insights Trace Requested"))
		bRequestsInsightsTrace = true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoCSVProfiler")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSV Profiler Requested"))
		bRequestsCSVProfiler = true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoFPSChart")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("FPSCharts Requested"))
		bRequestsFPSChart = true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoVideoCapture")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Video Capture Requested"))
		bRequestsVideoCapture = true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.LockDynamicRes")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Locking dynamic res requested"))
		bRequestsLockedDynRes = true;
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.ArtifactOutputPath="), ArtifactOutputPath))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Artifact Output Path: %s"), *ArtifactOutputPath)
	}
	
	FWorldDelegates::OnPreWorldInitialization.AddUObject(this, &ThisClass::OnPreWorldInitializeInternal);

	InitializeInsights();

	UE_LOG(LogGauntlet, Log, TEXT("Initializing Test: %s"), *GetTestID());
}

void UAutomatedPerfTestControllerBase::OnTick(float TimeDelta)
{
	Super::OnTick(TimeDelta);
	
	MarkHeartbeatActive();
}

void UAutomatedPerfTestControllerBase::OnStateChange(FName OldState, FName NewState)
{
	Super::OnStateChange(OldState, NewState);
}

void UAutomatedPerfTestControllerBase::OnPreMapChange()
{
	Super::OnPreMapChange();
}

void UAutomatedPerfTestControllerBase::BeginDestroy()
{
	UnbindAllDelegates();
	
	Super::BeginDestroy();
}

void UAutomatedPerfTestControllerBase::EndAutomatedPerfTest(const int32 ExitCode)
{
	UnbindAllDelegates();
	ShutdownInsights();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Test ID %s completed, requesting exit..."), *GetTestID());
	
	EndTest(ExitCode);
}

void UAutomatedPerfTestControllerBase::OnVideoRecordingFinalized(bool Succeeded, const FString& FilePath)
{
	if(!Succeeded)
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video Capture finalized, but did not succeed"))
	}
/* TODO moving this to automation layer due to file access restrictions
	FString SrcFilePath;
	FString FileName;
	FString Extension;
	FPaths::Split(FilePath, SrcFilePath, FileName, Extension);

	FString DestFileName = FileName + "." + Extension;
	
	const FString DestinationDir = FPaths::Combine(FPaths::ProjectSavedDir(), "Videos");
	const FString DestinationFilePath = FPaths::Combine(DestinationDir, DestFileName);
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Copying video file %s to Saved: %s"), *FilePath, *DestinationFilePath);
	
	if(IFileManager::Get().Copy(*DestinationFilePath, *FilePath, 1, 1) != COPY_OK)
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Failed to copy video file"));
	}
	*/
}

void UAutomatedPerfTestControllerBase::UnbindAllDelegates()
{
	if(UWorld* const World = GetWorld())
	{
		World->OnWorldBeginPlay.RemoveAll(this);
		World->GameStateSetEvent.RemoveAll(this);
	}

#if CSV_PROFILER
	if (FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		CsvProfiler->OnCSVProfileFinished().Remove(CsvProfilerDelegateHandle);
	}
#endif // CSV_PROFILER

	if(RequestsVideoCapture())
	{
		if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
		{
			VideoRecordingSystem->GetOnVideoRecordingFinalizedDelegate().RemoveAll(this);
		}
	}
}

EAutomatedPerfTestCSVOutputMode UAutomatedPerfTestControllerBase::GetCSVOutputMode() const
{
	return CSVOutputMode;
}

void UAutomatedPerfTestControllerBase::ConsoleCommand(const TCHAR* Cmd)
{
	if (APlayerController* Controller = GetPlayerController())
	{
		Controller->ConsoleCommand(Cmd);
	}

	UE_LOG(LogAutomatedPerfTest, Display, TEXT("Issued ConsoleCommand '%s'"), Cmd);
}
