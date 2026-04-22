// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebugger.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Insights/IUnrealInsightsModule.h"
#include "IRewindDebuggerExtension.h"
#include "Kismet2/DebuggerCommands.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ObjectTrace.h"
#include "RewindDebuggerCommands.h"
#include "RewindDebuggerModule.h"
#include "RewindDebuggerObjectTrack.h"
#include "RewindDebuggerPlaceholderTrack.h"
#include "RewindDebuggerRuntime/RewindDebuggerRuntime.h"
#include "RewindDebuggerSettings.h"
#include "RewindDebuggerTrackCreators.h"
#include "SModalSessionBrowser.h"
#include "ToolMenuDelegates.h"
#include "ToolMenus.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "RewindDebugger"

static void IterateExtensions(TFunction<void(IRewindDebuggerExtension* Extension)> IteratorFunction)
{
	// update extensions
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(IRewindDebuggerExtension::ModularFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerExtension* Extension = static_cast<IRewindDebuggerExtension*>(ModularFeatures.GetModularFeatureImplementation(IRewindDebuggerExtension::ModularFeatureName, ExtensionIndex));
		IteratorFunction(Extension);
	}
}

static void TraceSubobjects(const UObject* OuterObject)
{
	TArray<UObject*> Subobjects;
	GetObjectsWithOuter(OuterObject, Subobjects, true);
	for (const UObject* Subobject : Subobjects)
	{
		TRACE_OBJECT_LIFETIME_BEGIN(Subobject);
	}
}

FRewindDebugger::FRewindDebugger()
{
	if (RewindDebugger::FRewindDebuggerRuntime::Instance() == nullptr)
	{
		RewindDebugger::FRewindDebuggerRuntime::Initialize();
	}

	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->ClearRecording.AddRaw(this, &FRewindDebugger::OnClearRecording);
		Runtime->RecordingStarted.AddRaw(this, &FRewindDebugger::OnRecordingStarted);
		Runtime->RecordingStopped.AddRaw(this, &FRewindDebugger::OnRecordingStopped);
	}

	RewindDebugger::FRewindDebuggerTrackCreators::EnumerateCreators([this](const RewindDebugger::IRewindDebuggerTrackCreator* Creator)
		{
			Creator->GetTrackTypes(TrackTypes);
		});

	RecordingDuration.Set(0);

	UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FRewindDebugger::OnPIEStarted);
	PausePIEHandle = FEditorDelegates::PausePIE.AddRaw(this, &FRewindDebugger::OnPIEPaused);
	ResumePIEHandle = FEditorDelegates::ResumePIE.AddRaw(this, &FRewindDebugger::OnPIEResumed);
	SingleStepPIEHandle = FEditorDelegates::SingleStepPIE.AddRaw(this, &FRewindDebugger::OnPIESingleStepped);
	// Using ShutdownPIE instead of EndPIE to make sure all traces emitted during world EndPlay get processed before disabling channels
	ShutdownPIEHandle = FEditorDelegates::ShutdownPIE.AddRaw(this, &FRewindDebugger::OnPIEStopped);

	RootObjectName.OnPropertyChanged = RootObjectName.OnPropertyChanged.CreateLambda([this](FString Target)
		{
			URewindDebuggerSettings& Settings = URewindDebuggerSettings::Get();
			if (Settings.DebugTargetActor != Target)
			{
				Settings.DebugTargetActor = Target;
				Settings.Modify();
				Settings.SaveConfig();
			}

			CandidateIds.SetNum(0);
			GetTargetObjectIds(CandidateIds);
			// make sure all the SubObjects of the root object have been traced
#if OBJECT_TRACE_ENABLED
			for (const RewindDebugger::FObjectId& TargetObjectId : CandidateIds)
			{
				if (const UObject* TargetObject = FObjectTrace::GetObjectFromId(TargetObjectId.GetMainId()))
				{
					TraceSubobjects(TargetObject);
				}
			}
#endif

			RefreshDebugTracks();
		});

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("RewindDebugger"), 0.0f, [this](float DeltaTime)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FRewindDebuggerModule_Tick);

			Tick(DeltaTime);

			return true;
		});
}

FRewindDebugger::~FRewindDebugger()
{
	FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
	FEditorDelegates::PausePIE.Remove(PausePIEHandle);
	FEditorDelegates::ResumePIE.Remove(ResumePIEHandle);
	FEditorDelegates::SingleStepPIE.Remove(SingleStepPIEHandle);
	FEditorDelegates::ShutdownPIE.Remove(ShutdownPIEHandle);

	FTSTicker::RemoveTicker(TickerHandle);

	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->RecordingStarted.RemoveAll(this);
	}
}

void FRewindDebugger::Initialize()
{
	FRewindDebugger* Instance = new FRewindDebugger;
	InternalInstance = Instance;

	// Triggering callbacks after assigning the global instance
	// so code called from the callback don't track to initialize the instance again.
	if (GEditor->bIsSimulatingInEditor || GEditor->PlayWorld)
	{
		Instance->OnPIEStarted(true);
	}
}

void FRewindDebugger::Shutdown()
{
	delete InternalInstance;
}

void FRewindDebugger::SetTrackListChangedDelegate(const FOnTrackListChanged& InTrackListChangedDelegate)
{
	TrackListChangedDelegate = InTrackListChangedDelegate;
}

void FRewindDebugger::SetTrackCursorDelegate(const FOnTrackCursor& InTrackCursorDelegate)
{
	TrackCursorDelegate = InTrackCursorDelegate;
}

void FRewindDebugger::OnPIEStarted(bool bSimulating)
{
	bPIEStarted = true;
	bPIESimulating = true;

	if (ShouldAutoRecordOnPIE())
	{
		StartRecording();
	}
}

void FRewindDebugger::OnPIEPaused(bool bSimulating)
{
	bPIESimulating = false;
	ControlState = EControlState::Pause;

#if OBJECT_TRACE_ENABLED
	if (IsRecording())
	{
		const UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
	}
#endif // OBJECT_TRACE_ENABLED

	if (ShouldAutoEject() && FPlayWorldCommandCallbacks::IsInPIE())
	{
		bool CanEject = false;
		for (auto It = GUnrealEd->SlatePlayInEditorMap.CreateIterator(); It; ++It)
		{
			CanEject = CanEject || It.Value().DestinationSlateViewport.IsValid();
		}

		if (CanEject)
		{
			GEditor->RequestToggleBetweenPIEandSIE();
		}
	}
}

void FRewindDebugger::OnPIEResumed(bool bSimulating)
{
	bPIESimulating = true;

	if (ShouldAutoEject() && FPlayWorldCommandCallbacks::IsInSIE())
	{
		GEditor->RequestToggleBetweenPIEandSIE();
	}
}

void FRewindDebugger::OnPIESingleStepped(bool bSimulating)
{
#if OBJECT_TRACE_ENABLED
	if (IsRecording())
	{
		const UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
	}
#endif // OBJECT_TRACE_ENABLED
}


void FRewindDebugger::OnPIEStopped(bool bSimulating)
{
	if (IsRecording() && bPIESimulating)
	{
#if OBJECT_TRACE_ENABLED
		const UWorld* World = GetWorldToVisualize();
		SetCurrentScrubTime(FObjectTrace::GetWorldElapsedTime(World));
#endif // OBJECT_TRACE_ENABLED
	}

	bPIEStarted = false;
	bPIESimulating = false;

	StopRecording();

	bDisplayWorldIdValid = false;
}

bool FRewindDebugger::GetRootObjectPosition(FVector& OutPosition) const
{
	OutPosition = RootObjectPosition.Get(FVector::ZeroVector);
	return RootObjectPosition.IsSet();
}

void FRewindDebugger::SetRootObjectPosition(const TOptional<FVector>& InPosition)
{
	RootObjectPosition = InPosition;
}

void FRewindDebugger::GetTargetObjectIds(TArray<RewindDebugger::FObjectId>& OutTargetObjectIds) const
{
	OutTargetObjectIds.Empty(2);

	if (RootObjectName.Get() == "")
	{
		return;
	}

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			GameplayProvider->EnumerateObjects(CurrentTraceRange.GetLowerBoundValue(), CurrentTraceRange.GetUpperBoundValue(), [this, &OutTargetObjectIds](const FObjectInfo& InObjectInfo)
				{
					if (RootObjectName.Get() == InObjectInfo.Name)
					{
						OutTargetObjectIds.Add(InObjectInfo.GetId());
					}
				});
		}
	}

	// make sure all the SubObjects of the root object have been traced
#if OBJECT_TRACE_ENABLED
	if (IsRecording())
	{
		for (const RewindDebugger::FObjectId& CandidateId : CandidateIds)
		{
			if (const UObject* TargetObject = FObjectTrace::GetObjectFromId(CandidateId.GetMainId()))
			{
				TraceSubobjects(TargetObject);
			}
		}
	}
#endif
}

void FRewindDebugger::RefreshDebugTracks()
{
	static const FName DebugMessageTrackName = "DebugMessageDummyTrack";
	TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::RefreshDebugTracks);

	if (CandidateIds.Num() == 0)
	{
		GetTargetObjectIds(CandidateIds);
	}

	const FString SelectionName = RootObjectName.Get();

	if (CandidateIds.Num() == 0 && !SelectionName.IsEmpty())
	{
		// fallback code path for when the target object is not found

		if (DebugTracks.Num() != 2)
		{
			// clear tracks so we don't show data from previous recordings
			DebugTracks.SetNum(0);
			DebugTracks.SetNum(2);
		}

		if (DebugTracks[1] == nullptr || DebugTracks[0] == nullptr || DebugTracks[0]->GetName().ToString() != SelectionName)
		{
			DebugTracks[0] = MakeShared<FRewindDebuggerPlaceholderTrack>(FName(SelectionName), FText::FromString(SelectionName));
			DebugTracks[1] = MakeShared<FRewindDebuggerPlaceholderTrack>(DebugMessageTrackName, NSLOCTEXT("RewindDebugger", "No Debug Data", " - Start a recording to debug"));
			OnTrackListChanged();
		}
	}
	else if (DebugTracks.Num() || CandidateIds.Num())
	{
		bool bChanged = false;

		// remove any existing tracks that don't match the current list of object ids
		for (int TrackIndex = DebugTracks.Num() - 1; TrackIndex >= 0; TrackIndex--)
		{
			if (!CandidateIds.Contains(DebugTracks[TrackIndex]->GetAssociatedObjectId()))
			{
				DebugTracks.RemoveAt(TrackIndex);
			}
		}

		// add new tracks for current list of object identifiers if they don't already exist
		for (const RewindDebugger::FObjectId& CandidateIdentifier : CandidateIds)
		{
			const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>* FoundTrack = DebugTracks.FindByPredicate(
				[CandidateIdentifier](const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& Track)
				{
					return Track->GetAssociatedObjectId() == CandidateIdentifier;
				});

			if (!FoundTrack)
			{
				DebugTracks.Add(MakeShared<RewindDebugger::FRewindDebuggerObjectTrack>(CandidateIdentifier, RootObjectName.Get(), true));
				bChanged = true;
			}
		}

		// update all tracks
		for (const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& DebugTrack : DebugTracks)
		{
			if (DebugTrack->Update())
			{
				UE_LOG(LogRewindDebugger, Verbose, TEXT("List changed by: '%s'"), *DebugTrack->GetDisplayName().ToString());
				bChanged = true;
			}
		}

		if (bChanged)
		{
			OnTrackListChanged();
		}
	}
}

void FRewindDebugger::OnConnection()
{
	// queue up some operations to happen on the game thread next tick
	bTraceJustConnected = true;
	FTraceAuxiliary::OnConnection.RemoveAll(this);
}

bool FRewindDebugger::CanStartRecording() const
{
	return !IsRecording() && bPIESimulating;
}

void FRewindDebugger::StartRecording() const
{
	if (!CanStartRecording())
	{
		return;
	}

	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->StartRecording();
	}
}

void FRewindDebugger::OnClearRecording()
{
	ClearTrace();
	RecordingDuration.Set(0);
	CandidateIds.Empty(2);
	RootObjectPosition.Reset();

	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->Clear(this);
		}
	);
}

void FRewindDebugger::OnRecordingStarted()
{
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->RecordingStarted(this);
		}
	);

	UnrealInsightsModule->StartAnalysisForLastLiveSession(5.0);
}

void FRewindDebugger::OnRecordingStopped()
{
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->RecordingStopped(this);
		}
	);
}

bool FRewindDebugger::CanOpenTrace() const
{
	return !bPIEStarted;
}


void FRewindDebugger::OpenTrace(const FString& FilePath)
{
	ClearTrace();

	bDisplayWorldIdValid = false;

	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	TraceInsightsModule.StartAnalysisForTraceFile(*FilePath);

	// todo: optionally open the map the trace file was recorded in
}

void FRewindDebugger::OpenTrace()
{
	const FString FolderPath = "";

	TArray<FString> OutOpenFilenames;
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("Unreal Trace|*.utrace|");

		DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("OpenDialogTitle", "Open Rewind Debugger Recording").ToString(),
			FolderPath,
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OutOpenFilenames
		);
	}

	if (OutOpenFilenames.Num() > 0)
	{
		if (OutOpenFilenames[0].EndsWith(TEXT("utrace")))
		{
			OpenTrace(OutOpenFilenames[0]);
		}
	}
}


void FRewindDebugger::AttachToSession()
{
	ClearTrace();
	const TSharedRef<SModalSessionBrowser> SessionBrowserModal = SNew(SModalSessionBrowser);

	if (SessionBrowserModal->ShowModal() != EAppReturnType::Cancel)
	{
		bool bSuccess = false;
		const SModalSessionBrowser::FTraceSessionInfo SessionInfo = SessionBrowserModal->GetSelectedTraceInfo();
		if (SessionInfo.bIsValid)
		{
			const FString SessionAddress = SessionBrowserModal->GetSelectedTraceStoreAddress();
			IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			TraceInsightsModule.StartAnalysisForTrace(SessionInfo.TraceID);
			bSuccess = TraceInsightsModule.GetAnalysisSession().IsValid();
		}

		if (!bSuccess)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FailedToConnectToSessionMessage", "Failed to connect to session"));
		}
	}
}

bool FRewindDebugger::CanClearTrace() const
{
	return GetAnalysisSession() != nullptr;
}

void FRewindDebugger::ClearTrace()
{
	StopRecording();
	RecordingDuration.Set(0);

	CandidateIds.Empty();
	CurrentTraceRange.SetLowerBoundValue(0);
	CurrentTraceRange.SetUpperBoundValue(0);
	RecordingDuration.Set(0.0);
	SetCurrentScrubTime(0.0);

	TrackSelectionChanged(nullptr);

	// update extensions
	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->Clear(this);
		}
	);

	IUnrealInsightsModule& TraceInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	// only way I can find to clear the session is trying to load a name that doesn't exist.
	TraceInsightsModule.StartAnalysisForTraceFile(TEXT("0"));

	RefreshDebugTracks();
}

bool FRewindDebugger::CanSaveTrace() const
{
	const TraceServices::IAnalysisSession* Session = GetAnalysisSession();
	return Session != nullptr && Session->IsAnalysisComplete();
}

void FRewindDebugger::SaveTrace(FString FileName)
{
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (Session->IsAnalysisComplete())
		{
			const FString SourceFileName = Session->GetName();

			FPlatformFileManager& FileManager = FPlatformFileManager::Get();
			IPlatformFile& PlatformFile = FileManager.GetPlatformFile();

			PlatformFile.CopyFile(*FileName, *SourceFileName);
		}

	}
}

void FRewindDebugger::SaveTrace()
{
	const FString FolderPath = "";

	TArray<FString> OutFilenames;
	if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
	{
		FString ExtensionStr;
		ExtensionStr += TEXT("Rewind Debugger Recording |*.utrace|");

		DesktopPlatform->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LOCTEXT("SaveDialogTitle", "Save Rewind Debugger Recording").ToString(),
			FolderPath,
			TEXT(""),
			*ExtensionStr,
			EFileDialogFlags::None,
			OutFilenames
		);
	}

	if (OutFilenames.Num() > 0)
	{
		if (OutFilenames[0].EndsWith(TEXT(".utrace")))
		{
			SaveTrace(OutFilenames[0]);
		}
	}
}

bool FRewindDebugger::ShouldAutoRecordOnPIE() const
{
	return URewindDebuggerSettings::Get().bShouldAutoRecordOnPIE;
}

void FRewindDebugger::SetShouldAutoRecordOnPIE(bool value)
{
	URewindDebuggerSettings& RewindDebuggerSettings = URewindDebuggerSettings::Get();
	RewindDebuggerSettings.Modify();
	RewindDebuggerSettings.bShouldAutoRecordOnPIE = value;
	RewindDebuggerSettings.SaveConfig();
}

bool FRewindDebugger::ShouldAutoEject() const
{
	return URewindDebuggerSettings::Get().bShouldAutoEject;
}

void FRewindDebugger::SetShouldAutoEject(bool value)
{
	URewindDebuggerSettings& RewindDebuggerSettings = URewindDebuggerSettings::Get();
	RewindDebuggerSettings.Modify();
	RewindDebuggerSettings.bShouldAutoEject = value;
	RewindDebuggerSettings.SaveConfig();
}

void FRewindDebugger::StopRecording()
{
	if (RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		Runtime->StopRecording();
	}
}

bool FRewindDebugger::CanPause() const
{
	return ControlState != EControlState::Pause;
}

void FRewindDebugger::Pause()
{
	if (CanPause())
	{
		if (bPIESimulating)
		{
			// pause PIE
		}

		ControlState = EControlState::Pause;
	}
}

bool FRewindDebugger::IsPlaying() const
{
	return ControlState == EControlState::Play && !bPIESimulating;
}

bool FRewindDebugger::CanPlay() const
{
	return ControlState != EControlState::Play && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::Play()
{
	if (CanPlay())
	{
		if (CurrentScrubTime >= RecordingDuration.Get())
		{
			SetCurrentScrubTime(0);
		}

		ControlState = EControlState::Play;
	}
}

bool FRewindDebugger::CanPlayReverse() const
{
	return ControlState != EControlState::PlayReverse && !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::PlayReverse()
{
	if (CanPlayReverse())
	{
		if (CurrentScrubTime <= 0)
		{
			SetCurrentScrubTime(RecordingDuration.Get());
		}

		ControlState = EControlState::PlayReverse;
	}
}

bool FRewindDebugger::CanScrub() const
{
	return !bPIESimulating && RecordingDuration.Get() > 0;
}

void FRewindDebugger::ScrubToStart()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(0);
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::ScrubToEnd()
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(RecordingDuration.Get());
		TrackCursorDelegate.ExecuteIfBound(false);
	}
}

void FRewindDebugger::Step(const int32 InNumberOfFrames)
{
	if (CanScrub())
	{
		Pause();

		if (FMath::Abs(InNumberOfFrames) == 1
			&& SelectedTrack)
		{
			const TOptional<double> NewScrubTime = SelectedTrack->GetStepFrameTime(
				InNumberOfFrames == 1
					? RewindDebugger::EStepMode::Forward
					: RewindDebugger::EStepMode::Backward
				, CurrentScrubTime);

			if (NewScrubTime.IsSet())
			{
				SetCurrentScrubTime(NewScrubTime.GetValue());
				TrackCursorDelegate.ExecuteIfBound(false);
				return;
			}
		}

		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
			if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
			{
				if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(RecordingIndex))
				{
					const uint64 EventCount = Recording->GetEventCount();

					if (EventCount > 0)
					{
						ScrubTimeInformation.FrameIndex = FMath::Clamp<int64>(ScrubTimeInformation.FrameIndex + InNumberOfFrames, 0, (int64)EventCount - 1);
						const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubTimeInformation.FrameIndex);

						SetCurrentScrubTime(Event.ElapsedTime);

						TrackCursorDelegate.ExecuteIfBound(false);
					}
				}
			}
		}
	}
}

void FRewindDebugger::StepForward()
{
	Step(1);
}

void FRewindDebugger::StepBackward()
{
	Step(-1);
}

void FRewindDebugger::ScrubToTime(double ScrubTime, bool bIsScrubbing)
{
	if (CanScrub())
	{
		Pause();
		SetCurrentScrubTime(ScrubTime);
	}
}

UWorld* FRewindDebugger::GetWorldToVisualize() const
{
	// we probably want to replace this with a world selector widget, if we are going to support tracing from anything other thn the PIE world

	UWorld* World = nullptr;

	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (GIsEditor && EditorEngine != nullptr && World == nullptr)
	{
		// let's use PlayWorld during PIE/Simulate and regular world from editor otherwise, to draw debug information
		World = EditorEngine->PlayWorld != nullptr ? ToRawPtr(EditorEngine->PlayWorld) : EditorEngine->GetEditorWorldContext().World();
	}

	return World;
}

bool FRewindDebugger::IsRecording() const
{
	if (const RewindDebugger::FRewindDebuggerRuntime* Runtime = RewindDebugger::FRewindDebuggerRuntime::Instance())
	{
		return Runtime->IsRecording();
	}
	return false;
}

bool FRewindDebugger::IsTraceFileLoaded() const
{
	return GetAnalysisSession() != nullptr && !bPIEStarted;
}

void FRewindDebugger::SetCurrentViewRange(const TRange<double>& Range)
{
	CurrentViewRange = Range;
	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentViewRange.GetLowerBoundValue(), LowerBoundViewTimeInformation, RecordingIndex, Session);
		GetScrubTimeInformation(CurrentViewRange.GetUpperBoundValue(), UpperBoundViewTimeInformation, RecordingIndex, Session);

		CurrentTraceRange.SetLowerBoundValue(LowerBoundViewTimeInformation.ProfileTime);
		CurrentTraceRange.SetUpperBoundValue(UpperBoundViewTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::SetCurrentScrubTime(double Time)
{
	CurrentScrubTime = Time;

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		GetScrubTimeInformation(CurrentScrubTime, ScrubTimeInformation, RecordingIndex, Session);

		TraceTime.Set(ScrubTimeInformation.ProfileTime);
	}
}

void FRewindDebugger::GetScrubTimeInformation(double InDebugTime, FScrubTimeInformation& InOutTimeInformation, uint16 InRecordingIndex, const TraceServices::IAnalysisSession* AnalysisSession)
{
	const IGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<IGameplayProvider>("GameplayProvider");
	const IAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<IAnimationProvider>("AnimationProvider");

	if (GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		if (const IGameplayProvider::RecordingInfoTimeline* Recording = GameplayProvider->GetRecordingInfo(InRecordingIndex))
		{
			const uint64 EventCount = Recording->GetEventCount();

			if (EventCount > 0)
			{
				int ScrubFrameIndex = InOutTimeInformation.FrameIndex;
				const FRecordingInfoMessage& FirstEvent = Recording->GetEvent(0);
				const FRecordingInfoMessage& LastEvent = Recording->GetEvent(EventCount - 1);

				// Check if we are outside the recorded range, and apply the first or last frame
				if (InDebugTime <= FirstEvent.ElapsedTime)
				{
					ScrubFrameIndex = FMath::Min<uint64>(1, EventCount - 1);
				}
				else if (InDebugTime >= LastEvent.ElapsedTime)
				{
					ScrubFrameIndex = EventCount - 1;
				}
				// Find the two keys surrounding the InDebugTime, and pick the nearest to update InOutTimeInformation
				else
				{
					const FRecordingInfoMessage& ScrubEvent = Recording->GetEvent(ScrubFrameIndex);
					constexpr float MaxTimeDifferenceInSeconds = 15.0f / 60.0f;

					// Use linear search on smaller time differences
					if (FMath::Abs(InDebugTime - ScrubEvent.ElapsedTime) <= MaxTimeDifferenceInSeconds)
					{
						if (Recording->GetEvent(ScrubFrameIndex).ElapsedTime > InDebugTime)
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex > 0; EventIndex--)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex - 1);
								if (Event.ElapsedTime >= InDebugTime && NextEvent.ElapsedTime <= InDebugTime)
								{
									if (Event.ElapsedTime - InDebugTime < InDebugTime - NextEvent.ElapsedTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex - 1;
									}
									break;
								}
							}
						}
						else
						{
							for (uint64 EventIndex = ScrubFrameIndex; EventIndex < EventCount - 1; EventIndex++)
							{
								const FRecordingInfoMessage& Event = Recording->GetEvent(EventIndex);
								const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EventIndex + 1);
								if (Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime)
								{
									if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
									{
										ScrubFrameIndex = EventIndex;
									}
									else
									{
										ScrubFrameIndex = EventIndex + 1;
									}
									break;
								}
							}
						}
					}
					// Binary search for surrounding keys on big time differences
					else
					{
						uint64 StartEventIndex = 0;
						uint64 EndEventIndex = EventCount - 1;

						while (EndEventIndex - StartEventIndex > 1)
						{
							const uint64 MiddleEventIndex = ((StartEventIndex + EndEventIndex) / 2);
							const FRecordingInfoMessage& MiddleEvent = Recording->GetEvent(MiddleEventIndex);
							if (InDebugTime < MiddleEvent.ElapsedTime)
							{
								EndEventIndex = MiddleEventIndex;
							}
							else
							{
								StartEventIndex = MiddleEventIndex;
							}
						}

						// Ensure there is not frames between start and end index
						check(EndEventIndex == StartEventIndex + 1);

						const FRecordingInfoMessage& Event = Recording->GetEvent(StartEventIndex);
						const FRecordingInfoMessage& NextEvent = Recording->GetEvent(EndEventIndex);

						// Ensure debug time is between both frames time range
						check(Event.ElapsedTime <= InDebugTime && NextEvent.ElapsedTime >= InDebugTime);

						// Choose frame that is nearest to the debug time
						if (InDebugTime - Event.ElapsedTime < NextEvent.ElapsedTime - InDebugTime)
						{
							ScrubFrameIndex = StartEventIndex;
						}
						else
						{
							ScrubFrameIndex = EndEventIndex;
						}
					}
				}

				const FRecordingInfoMessage& Event = Recording->GetEvent(ScrubFrameIndex);
				InOutTimeInformation.FrameIndex = ScrubFrameIndex;
				InOutTimeInformation.ProfileTime = Event.ProfileTime;
			}
		}
	}
}

const TraceServices::IAnalysisSession* FRewindDebugger::GetAnalysisSession() const
{
	if (UnrealInsightsModule == nullptr)
	{
		UnrealInsightsModule = &FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	}

	return UnrealInsightsModule ? UnrealInsightsModule->GetAnalysisSession().Get() : nullptr;
}

uint64 FRewindDebugger::GetRootObjectId() const
{
	return DebuggedObjects.Num() ? DebuggedObjects[0]->GetUObjectId() : RewindDebugger::FObjectId::InvalidId;
}

const FObjectInfo* FRewindDebugger::FindTypedOuterInfo(TNotNull<const UStruct*> InType, TNotNull<const IGameplayProvider*> InGameplayProvider, const uint64 InObjectId) const
{
	const FClassInfo* TypeInfo = InGameplayProvider->FindClassInfo(*InType->GetPathName());

	uint64 ObjectId(InObjectId);
	while (true)
	{
		const FObjectInfo& ObjectInfo = InGameplayProvider->GetObjectInfo(ObjectId);
		if (InGameplayProvider->IsSubClassOf(ObjectInfo.ClassId, TypeInfo->Id))
		{
			return &ObjectInfo;
		}

		if (!ObjectInfo.GetOuterId().IsSet())
		{
			return nullptr;
		}

		ObjectId = ObjectInfo.GetOuterUObjectId();
	}
}

void FRewindDebugger::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick);

	if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
	{
		if (const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider"))
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

			// set a default display world when loading a trace (first client/standalone world)
			if (IsTraceFileLoaded() && !bDisplayWorldIdValid)
			{
				GameplayProvider->EnumerateWorlds([this](const FWorldInfo& WorldInfo)
					{
						if (WorldInfo.Type == FWorldInfo::EType::PIE)
						{
							if (WorldInfo.NetMode == FWorldInfo::ENetMode::Client && WorldInfo.PIEInstanceId == 1)
							{
								DisplayWorldId = WorldInfo.Id;
								bDisplayWorldIdValid = true;
							}
							if (WorldInfo.NetMode == FWorldInfo::ENetMode::Standalone && WorldInfo.PIEInstanceId == 0)
							{
								DisplayWorldId = WorldInfo.Id;
								bDisplayWorldIdValid = true;
							}
						}
						else if (WorldInfo.Type == FWorldInfo::EType::Game)
						{
							DisplayWorldId = WorldInfo.Id;
							bDisplayWorldIdValid = true;
						}
					});
			}

			const double RecordingDurationValue = GameplayProvider->GetRecordingDuration();
			if (IsTraceFileLoaded() && RecordingDurationValue > RecordingDuration.Get())
			{
				// while trace file is loading up, force the trace range to update.
				SetCurrentViewRange(GetCurrentViewRange());
			}
			RecordingDuration.Set(RecordingDurationValue);

			RefreshDebugTracks();

			if (bPIESimulating)
			{
				if (IsRecording())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick_UpdateSimulating);
					SetCurrentScrubTime(RecordingDurationValue);
					TrackCursorDelegate.ExecuteIfBound(false);
				}

				// The debug position is only expected to be set outside of PIE
				RootObjectPosition.Reset();
			}
			else
			{
				if (RecordingDuration.Get() > 0 && CurrentScrubTime <= RecordingDuration.Get())
				{
					if (ControlState == EControlState::Play || ControlState == EControlState::PlayReverse)
					{
						const float PlaybackRate = URewindDebuggerSettings::Get().PlaybackRate;
						TRACE_CPUPROFILER_EVENT_SCOPE(FRewindDebugger::Tick_UpdatePlayback);
						const float Rate = PlaybackRate * (ControlState == EControlState::Play ? 1 : -1);
						SetCurrentScrubTime(FMath::Clamp(CurrentScrubTime + Rate * DeltaTime, 0.0f, RecordingDuration.Get()));
						TrackCursorDelegate.ExecuteIfBound(Rate < 0);

						if (CurrentScrubTime == 0 || CurrentScrubTime == RecordingDuration.Get())
						{
							// pause at end.
							ControlState = EControlState::Pause;
						}
					}

					// update trace time
					SetCurrentScrubTime(CurrentScrubTime);
				}
			}
		}

		// update extensions
		IterateExtensions([DeltaTime, this](IRewindDebuggerExtension* Extension)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Extension->GetName());
				Extension->Update(DeltaTime, this);
			}
		);
	}
}

void FRewindDebugger::OnTrackListChanged()
{
	TrackListChangedDelegate.ExecuteIfBound();

	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*Extension->GetName());
			Extension->OnTrackListChanged(this);
		}
	);
}

void FRewindDebugger::OpenDetailsPanel()
{
	bIsDetailsPanelOpen = true;
	TrackSelectionChanged(SelectedTrack);
}

void FRewindDebugger::TrackSelectionChanged(const TSharedPtr<RewindDebugger::FRewindDebuggerTrack> InSelectedTrack)
{
	SelectedTrack = InSelectedTrack;

	if (bIsDetailsPanelOpen)
	{
		const FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		const TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		// if we now have no selection, don't force the tab into focus - this happens when tracks disappear and can cause PIE to lose focus while playing
		const bool bInvokeAsInactive = !SelectedTrack.IsValid();
		const TSharedPtr<SDockTab> DetailsTab = LevelEditorTabManager->TryInvokeTab(FRewindDebuggerModule::DetailsTabName, bInvokeAsInactive);

		if (DetailsTab.IsValid())
		{
			UpdateDetailsPanel(DetailsTab.ToSharedRef());
		}
	}
}

void FRewindDebugger::UpdateDetailsPanel(TSharedRef<SDockTab> DetailsTab)
{
	if (bIsDetailsPanelOpen)
	{
		TSharedPtr<SWidget> DetailsView;

		if (SelectedTrack)
		{
			DetailsView = SelectedTrack->GetDetailsView();
		}

		if (DetailsView)
		{
			DetailsTab->SetContent(DetailsView.ToSharedRef());
		}
		else
		{
			static TSharedPtr<SWidget> EmptyDetails;
			if (EmptyDetails == nullptr)
			{
				EmptyDetails = SNew(SSpacer);
			}
			DetailsTab->SetContent(EmptyDetails.ToSharedRef());
		}
	}
}

void FRewindDebugger::RegisterTrackContextMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->FindMenu(FRewindDebuggerModule::TrackContextMenuName);

	FToolMenuSection& Section = Menu->FindOrAddSection("SelectedTrack");

	Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			const URewindDebuggerTrackContextMenuContext* Context = InSection.FindContext<URewindDebuggerTrackContextMenuContext>();
			if (Context && Context->SelectedTrack.IsValid())
			{
				Context->SelectedTrack->BuildContextMenu(InSection);
			}
		}));
}

void FRewindDebugger::MakeOtherWorldsMenu(UToolMenu* Menu)
{
	const FRewindDebugger* RewindDebugger = Instance();

	FToolMenuSection& Section = Menu->AddSection("Other Worlds", LOCTEXT("Other Worlds", "Other Worlds"));

	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

		GameplayProvider->EnumerateWorlds([GameplayProvider, &Section](const FWorldInfo& WorldInfo)
			{
				const FObjectInfo* ObjectInfo = GameplayProvider->FindObjectInfo(WorldInfo.Id);
				FString Name = ObjectInfo->Name;

				if (WorldInfo.NetMode == FWorldInfo::ENetMode::DedicatedServer)
				{
					return;
				}
				else if (WorldInfo.Type == FWorldInfo::EType::Game || WorldInfo.Type == FWorldInfo::EType::PIE)
				{
					return;
				}
				else
				{
					if (WorldInfo.Type == FWorldInfo::EType::Editor)
					{
						Name = Name + " (Editor)";
					}
					else if (WorldInfo.Type == FWorldInfo::EType::Inactive)
					{
						Name = Name + " (Editor)";
					}
					else if (WorldInfo.Type == FWorldInfo::EType::EditorPreview)
					{
						Name = Name + " (Editor Preview)";
					}
					else if (WorldInfo.Type == FWorldInfo::EType::GamePreview)
					{
						Name = Name + " (Game Preview)";
					}
					else if (WorldInfo.Type == FWorldInfo::EType::GameRPC)
					{
						Name = Name + " (Game RPC)";
					}
				}

				Section.AddMenuEntry(FName(ObjectInfo->Name, WorldInfo.Id),
					FText::FromString(Name),
					FText(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([World = WorldInfo.Id]()
						{
							Instance()->SetDisplayWorld(World);
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([World = WorldInfo.Id]()
							{
								return Instance()->DisplayWorldId == World;
							})),
					EUserInterfaceActionType::Check
				);

			});
	}
}

void FRewindDebugger::SetDisplayWorld(uint64 WorldId)
{
	DisplayWorldId = WorldId;

	IterateExtensions([this](IRewindDebuggerExtension* Extension)
		{
			Extension->Clear(this);
			Extension->Update(0.0, this);
		});
}
void FRewindDebugger::MakeWorldsMenu(UToolMenu* Menu)
{
	const FRewindDebugger* RewindDebugger = Instance();

	FToolMenuSection& ServerWorldsSection = Menu->AddSection("Server Worlds", LOCTEXT("Server", "Server"));
	FToolMenuSection& GameWorldsSection = Menu->AddSection("Game Worlds", LOCTEXT("Game Worlds", "Game Worlds"));
	FToolMenuSection& OtherWorldsSection = Menu->AddSection("Other Worlds", LOCTEXT("Other Worlds", "Other Worlds"));

	OtherWorldsSection.AddSubMenu("Other Worlds",
		LOCTEXT("Other Worlds", "Other Worlds"),
		LOCTEXT("Other Worlds Tooltip", "Additional worlds such as  Editor Preview worlds"),
		FNewToolMenuChoice(
			FNewToolMenuDelegate::CreateStatic(MakeOtherWorldsMenu)
		));

	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");

		GameplayProvider->EnumerateWorlds([GameplayProvider, &GameWorldsSection, &OtherWorldsSection, &ServerWorldsSection](const FWorldInfo& WorldInfo)
			{
				const FObjectInfo* ObjectInfo = GameplayProvider->FindObjectInfo(WorldInfo.Id);
				FString Name = ObjectInfo->Name;

				FToolMenuSection* Section = &OtherWorldsSection;

				if (WorldInfo.NetMode == FWorldInfo::ENetMode::DedicatedServer)
				{
					Section = &ServerWorldsSection;
					Name = Name + " (Server)";
				}
				else if (WorldInfo.Type == FWorldInfo::EType::Game || WorldInfo.Type == FWorldInfo::EType::PIE)
				{
					Section = &GameWorldsSection;
					if (WorldInfo.NetMode == FWorldInfo::ENetMode::Client && WorldInfo.PIEInstanceId >= 0)
					{
						Name = Name + " (Client " + FString::FromInt(WorldInfo.PIEInstanceId) + ")";
					}
					if (WorldInfo.NetMode == FWorldInfo::ENetMode::Standalone && WorldInfo.PIEInstanceId >= 0)
					{
						Name = Name + " (Standalone " + FString::FromInt(WorldInfo.PIEInstanceId) + ")";
					}
				}
				else
				{
					return;
				}

				Section->AddMenuEntry(FName(ObjectInfo->Name, WorldInfo.Id),
					FText::FromString(Name),
					FText(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([World = WorldInfo.Id]()
						{
							Instance()->SetDisplayWorld(World);
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([World = WorldInfo.Id]()
							{
								return Instance()->DisplayWorldId == World;
							})),
					EUserInterfaceActionType::Check
				);
			});
	}
}

void FRewindDebugger::RegisterToolBar()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("RewindDebugger.ToolBar", NAME_None, EMultiBoxType::ToolBar);

	FToolMenuSection& Section = Menu->FindOrAddSection("VCRControls");
	const FRewindDebuggerCommands& Commands = FRewindDebuggerCommands::Get();
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.FirstFrame,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.FirstFrame.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.PreviousFrame,
		FText(),
		TAttribute<FText>::CreateLambda([]()
			{
				FText Override;
				if (Instance()->SelectedTrack.IsValid())
				{
					Override = Instance()->SelectedTrack->GetStepCommandTooltip(RewindDebugger::EStepMode::Backward);
				}

				return Override.IsEmpty() ? FRewindDebuggerCommands::Get().PreviousFrame->GetDescription() : Override;
			}),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.PreviousFrame.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.ReversePlay,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.ReversePlay.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.Pause,
		FText(),
		FText::Format(LOCTEXT("PauseButtonTooltip", "{0} ({1})"), Commands.Pause->GetDescription(), Commands.PauseOrPlay->GetInputText()),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.Pause.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.Play,
		FText(),
		FText::Format(LOCTEXT("PlayButtonTooltip", "{0} ({1})"), Commands.Play->GetDescription(), Commands.PauseOrPlay->GetInputText()),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.Play.small")));

	Section.AddEntry(
		FToolMenuEntry::InitComboButton(
			"PlaybackRate",
			FToolUIActionChoice(),
			FNewToolMenuChoice(
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* InNewToolMenu)
					{
						FToolMenuSection& Section = InNewToolMenu->AddSection("PlaybackSpeed", LOCTEXT("Playback Speed", "Playback Speed"));

						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"001", LOCTEXT("0.1", "0.1"), LOCTEXT("Set playback speed to 0.1", "Set playback speed to 0.1"), FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([]()
										{
											URewindDebuggerSettings::Get().PlaybackRate = 0.1;
										}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
										{
											return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.1);
										})
								)
								, EUserInterfaceActionType::RadioButton
							)
						);
						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"025", LOCTEXT("0.25", "0.25"), LOCTEXT("Set playback speed to 0.25", "Set playback speed to 0.25"), FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([]()
										{
											URewindDebuggerSettings::Get().PlaybackRate = 0.25;
										}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
										{
											return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.25);
										})
								)
								, EUserInterfaceActionType::RadioButton
							)
						);
						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"05", LOCTEXT("0.5", "0.5"), LOCTEXT("Set playback speed to 0.5", "Set playback speed to 0.5"), FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([]()
										{
											URewindDebuggerSettings::Get().PlaybackRate = 0.5;
										}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
										{
											return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 0.5);
										})
								)
								, EUserInterfaceActionType::RadioButton
							)
						);

						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"1", LOCTEXT("1", "1"), LOCTEXT("Set playback speed to 1", "Set playback speed to 1"), FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([]()
										{
											URewindDebuggerSettings::Get().PlaybackRate = 1;
										}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
										{
											return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 1);
										})
								)
								, EUserInterfaceActionType::RadioButton
							)
						);

						Section.AddEntry(
							FToolMenuEntry::InitMenuEntry(
								"2", LOCTEXT("2", "2"), LOCTEXT("Set playback speed to 2", "Set playback speed to 2"), FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([]()
										{
											URewindDebuggerSettings::Get().PlaybackRate = 2;
										}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([]
										{
											return FMath::IsNearlyEqual(URewindDebuggerSettings::Get().PlaybackRate, 2);
										})
								)
								, EUserInterfaceActionType::RadioButton
							)
						);

						Section.AddEntry(
							FToolMenuEntry::InitWidget(
								"EditInSequencerMenu",
								SNew(SNumericEntryBox<float>)
								.Value_Lambda([]()
									{
										return URewindDebuggerSettings::Get().PlaybackRate;
									})
								.OnValueChanged_Lambda([](float Value)
									{
										URewindDebuggerSettings::Get().PlaybackRate = Value;
									}),
								FText::GetEmpty(),
								true, false, true
							)
						);
					})
			),
			FText(),
			LOCTEXT("PlaybackRate_Tooltip", "Playback Options"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.PlaybackOptions")
		)
	);

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.NextFrame,
		FText(),
		TAttribute<FText>::CreateLambda([]()
			{
				FText Override;
				if (Instance()->SelectedTrack.IsValid())
				{
					Override = Instance()->SelectedTrack->GetStepCommandTooltip(RewindDebugger::EStepMode::Forward);
				}

				return Override.IsEmpty() ? FRewindDebuggerCommands::Get().NextFrame->GetDescription() : Override;
			}),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.NextFrame.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.LastFrame,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.LastFrame.small")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.StartRecording,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.StartRecording.small")));


	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.StopRecording,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.StopRecording.small")));

	Section.AddSeparator(NAME_None);

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.AttachToSession,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.ConnectToSession")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.OpenTrace,
		FText(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.SaveTrace,
		FText(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.ClearTrace,
		FText(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.AutoEject,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.AutoEject")));

	Section.AddSeparator(NAME_None);

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		Commands.AutoRecord,
		FText(),
		TAttribute<FText>(),
		FSlateIcon("RewindDebuggerStyle", "RewindDebugger.AutoRecord")));

	Section.AddSeparator("NAME_None");

	Section.AddEntry(FToolMenuEntry::InitComboButton(
		"Display World",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateLambda([]() { return Instance()->IsTraceFileLoaded(); })
		),
		FNewToolMenuDelegate::CreateStatic(&FRewindDebugger::MakeWorldsMenu),
		LOCTEXT("Display World", "Display World"),
		LOCTEXT("Display World Tooltip", "When loading trace files, only the objects (Such as Skeletal Meshes) from the world selected here will be spawned for preview")
	));

	Menu->SetStyleSet(&FAppStyle::Get());
	Menu->StyleName = "PaletteToolBar";
}


void FRewindDebugger::TrackDoubleClicked(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> InSelectedTrack)
{
	if (!InSelectedTrack.IsValid())
	{
		return;
	}

	SelectedTrack = InSelectedTrack;
	SelectedTrack->HandleDoubleClick();
}

TSharedPtr<SWidget> FRewindDebugger::BuildTrackContextMenu() const
{
	URewindDebuggerTrackContextMenuContext* MenuContext = NewObject<URewindDebuggerTrackContextMenuContext>();
	MenuContext->SelectedObject = GetSelectedObject();
	MenuContext->SelectedTrack = SelectedTrack;

	if (SelectedTrack.IsValid())
	{
		// build a list of class hierarchy names to make it easier for extensions to enable menu entries by type
		if (const TraceServices::IAnalysisSession* Session = GetAnalysisSession())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);

			const IGameplayProvider* GameplayProvider = Session->ReadProvider<IGameplayProvider>("GameplayProvider");
			const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(SelectedTrack->GetAssociatedObjectId());
			uint64 ClassId = ObjectInfo.ClassId;
			while (ClassId != 0)
			{
				const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);
				MenuContext->TypeHierarchy.Add(ClassInfo.Name);
				ClassId = ClassInfo.SuperId;
			}
		}
	}

	return UToolMenus::Get()->GenerateWidget(FRewindDebuggerModule::TrackContextMenuName, FToolMenuContext(MenuContext));
}


TSharedPtr<FDebugObjectInfo> FRewindDebugger::GetSelectedObject() const
{
	if (SelectedTrack.IsValid())
	{
		if (!SelectedObject.IsValid())
		{
			SelectedObject = MakeShared<FDebugObjectInfo>();
		}

		SelectedObject->Id = SelectedTrack->GetAssociatedObjectId();
		SelectedObject->ObjectName = SelectedTrack->GetDisplayName().ToString();
		return SelectedObject;
	}

	return TSharedPtr<FDebugObjectInfo>();
}

void FRewindDebugger::SetObjectToDebug(const RewindDebugger::FObjectId ObjectId)
{
	if (IsObjectCurrentlyDebugged(ObjectId.GetMainId()))
	{
		return;
	}

	if (GetAnalysisSession() == nullptr)
	{
		UE_LOG(LogRewindDebugger, Log, TEXT("Unable to set the object to debug since there is no active session"));
		return;
	}

	CandidateIds.Reset(1);
	CandidateIds.Push(ObjectId);

	RefreshDebugTracks();
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FRewindDebugger::GetSelectedTrack() const
{
	return SelectedTrack;
}

void FRewindDebugger::SelectTrack(RewindDebugger::FObjectId ObjectId)
{
	using namespace RewindDebugger;
	for (const TSharedPtr<FRewindDebuggerTrack>& Track : DebugTracks)
	{
		if (FRewindDebuggerTrack::Visit(Track, [this, ObjectId](const TSharedPtr<FRewindDebuggerTrack>& Track)
			{
				if (Track->GetAssociatedObjectId() == ObjectId)
				{
					TrackSelectionChanged(Track);
					return FRewindDebuggerTrack::EVisitResult::Break;
				}

				return FRewindDebuggerTrack::EVisitResult::Continue;
			}) == FRewindDebuggerTrack::EVisitResult::Break)
		{
			break;
		}
	}
}

// build a tree that's compatible with the public api from 5.0 for GetDebuggedObjects.
void FRewindDebugger::RefreshDebuggedObjects(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& InTracks, TArray<TSharedPtr<FDebugObjectInfo>>& OutObjects)
{
	using namespace RewindDebugger;

	OutObjects.SetNum(0, EAllowShrinking::No);
	for (const TSharedPtr<FRewindDebuggerTrack>& Track : InTracks)
	{
		FRewindDebuggerTrack::Visit(Track, [&OutObjects](const TSharedPtr<FRewindDebuggerTrack>& Track)
		{
			OutObjects.Add(MakeShared<FDebugObjectInfo>(Track->GetAssociatedObjectId(), Track->GetDisplayName().ToString()));
			return FRewindDebuggerTrack::EVisitResult::Continue;
		});
	}
}

TArray<TSharedPtr<FDebugObjectInfo>>& FRewindDebugger::GetDebuggedObjects()
{
	RefreshDebuggedObjects(DebugTracks, DebuggedObjects);
	return DebuggedObjects;
}

bool FRewindDebugger::IsObjectCurrentlyDebugged(uint64 InObjectId) const
{
	using namespace RewindDebugger;
	for (const TSharedPtr<FRewindDebuggerTrack>& Track : DebugTracks)
	{
		if (FRewindDebuggerTrack::Visit(Track, [InObjectId](TSharedPtr<FRewindDebuggerTrack> Track)
			{
				if (Track->GetUObjectId() == InObjectId)
				{
					return FRewindDebuggerTrack::EVisitResult::Break;
				}

				return FRewindDebuggerTrack::EVisitResult::Continue;
			}) == FRewindDebuggerTrack::EVisitResult::Break)
		{
			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
