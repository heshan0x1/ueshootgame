// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7

#include "BindableProperty.h"
#include "Containers/Ticker.h"
#include "IRewindDebugger.h"
#include "IRewindDebuggerTrackCreator.h"
#include "RewindDebuggerTrack.h"

namespace TraceServices
{
	class IAnalysisSession;
}
class FMenuBuilder;
class IGameplayProvider;
class SDockTab;
class SWidget;
class USkeletalMeshComponent;
struct FObjectInfo;

/**
 * Singleton class that handles the logic for the Rewind Debugger:
 * - Playback/Scrubbing state
 * - Start/Stop recording
 * - Keeping track of the current Debug Target object, and outputting a list of its sub objects/elements for the UI
 */
class FRewindDebugger : public IRewindDebugger
{
public:
	FRewindDebugger();
	virtual ~FRewindDebugger();

	//~ IRewindDebugger interface
	virtual double CurrentTraceTime() const override
	{
		return TraceTime.Get();
	}

	virtual double GetScrubTime() const override
	{
		return CurrentScrubTime;
	}

	virtual const TRange<double>& GetCurrentTraceRange() const override
	{
		return CurrentTraceRange;
	}

	virtual const TRange<double>& GetCurrentViewRange() const override
	{
		return CurrentViewRange;
	}

	virtual const TraceServices::IAnalysisSession* GetAnalysisSession() const override;
	virtual uint64 GetRootObjectId() const override;
	virtual bool GetRootObjectPosition(FVector& OutPosition) const override;
	virtual void SetRootObjectPosition(const TOptional<FVector>& InPosition) override;
	virtual UWorld* GetWorldToVisualize() const override;
	virtual bool IsRecording() const override;
	virtual bool IsPIESimulating() const override
	{
		return bPIESimulating;
	}

	virtual bool IsTraceFileLoaded() const override;
	virtual double GetRecordingDuration() const override
	{
		return RecordingDuration.Get();
	}

	virtual TSharedPtr<FDebugObjectInfo> GetSelectedObject() const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> GetSelectedTrack() const override;
	virtual void SelectTrack(RewindDebugger::FObjectId ObjectId) override;
	virtual void SetObjectToDebug(RewindDebugger::FObjectId ObjectId) override;
	virtual TArray<TSharedPtr<FDebugObjectInfo>>& GetDebuggedObjects() override;
	virtual bool IsObjectCurrentlyDebugged(uint64 ObjectId) const override;
	virtual bool ShouldDisplayWorld(uint64 WorldId) override
	{
		return DisplayWorldId == WorldId;
	}

	void OnConnection();

	void GetTargetObjectIds(TArray<RewindDebugger::FObjectId>& OutTargetObjectIds) const;

	TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& GetTracks()
	{
		return DebugTracks;
	}

	/** create singleton instance */
	static void Initialize();

	/** destroy singleton instance */
	static void Shutdown();

	/** get singleton instance */
	static FRewindDebugger* Instance()
	{
		return static_cast<FRewindDebugger*>(InternalInstance);
	}

	virtual bool CanStartRecording() const override;

	/** Start a new Recording:  Start tracing Objects, increment the current recording index, and reset the recording elapsed time to 0 */
	virtual void StartRecording() const override;
	void OnClearRecording();
	void OnRecordingStarted();
	void OnRecordingStopped();

	bool CanOpenTrace() const;
	void OpenTrace(const FString& FilePath);
	void OpenTrace();

	void AttachToSession();

	bool CanClearTrace() const;
	void ClearTrace();

	bool CanSaveTrace() const;
	void SaveTrace(FString FileName);
	void SaveTrace();

	bool ShouldAutoRecordOnPIE() const;
	void SetShouldAutoRecordOnPIE(bool InValue);

	bool ShouldAutoEject() const;
	void SetShouldAutoEject(bool InValue);

	/** Stop recording: Stop tracing Object + Animation Data. */
	void StopRecording();
	bool CanStopRecording() const
	{
		return IsRecording();
	}

	//~ VCR controls

	bool CanPause() const;
	void Pause();

	bool CanPlay() const;
	void Play();
	bool IsPlaying() const;

	bool CanPlayReverse() const;
	void PlayReverse();

	void ScrubToStart();
	void ScrubToEnd();
	void Step(int32 InNumberOfFrames);
	void StepForward();
	void StepBackward();

	bool CanScrub() const;
	void ScrubToTime(double ScrubTime, bool bIsScrubbing);

	/** Tick function: While recording, update recording duration.  While paused, and we have recorded data, update skinned mesh poses for the current frame, and handle playback. */
	void Tick(float DeltaTime);

	/** update the list of tracks for the currently selected debug target */
	void RefreshDebugTracks();

	void SetCurrentViewRange(const TRange<double>& Range);

	DECLARE_DELEGATE(FOnTrackListChanged)
	void SetTrackListChangedDelegate(const FOnTrackListChanged& InTrackListChangedDelegate);

	void TrackDoubleClicked(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> InSelectedTrack);
	void TrackSelectionChanged(TSharedPtr<RewindDebugger::FRewindDebuggerTrack> InSelectedTrack);
	TSharedPtr<SWidget> BuildTrackContextMenu() const;

	void UpdateDetailsPanel(TSharedRef<SDockTab> DetailsTab);
	static void RegisterTrackContextMenu();
	static void MakeOtherWorldsMenu(class UToolMenu* Menu);
	void SetDisplayWorld(uint64 WorldId);
	static void MakeWorldsMenu(class UToolMenu* Menu);
	static void RegisterToolBar();

	DECLARE_DELEGATE_OneParam(FOnTrackCursor, bool)
	void SetTrackCursorDelegate(const FOnTrackCursor& InTrackCursorDelegate);

	TBindableProperty<double>* GetTraceTimeProperty()
	{
		return &TraceTime;
	}

	TBindableProperty<double>* GetRecordingDurationProperty()
	{
		return &RecordingDuration;
	}

	TBindableProperty<FString, BindingType_Out>* GetRootObjectNameProperty()
	{
		return &RootObjectName;
	}

	UE_DEPRECATED(5.7, "Use GetRootObjectNameProperty instead")
	TBindableProperty<FString, BindingType_Out>* GetDebugTargetActorProperty()
	{
		return GetRootObjectNameProperty();
	}

	virtual void OpenDetailsPanel() override;
	void SetIsDetailsPanelOpen(bool bIsOpen)
	{
		bIsDetailsPanelOpen = bIsOpen;
	}

	virtual const FObjectInfo* FindTypedOuterInfo(TNotNull<const UStruct*> InType, TNotNull<const IGameplayProvider*> InGameplayProvider, uint64 InObjectId) const override;

	TArrayView<RewindDebugger::FRewindDebuggerTrackType> GetTrackTypes()
	{
		return TrackTypes;
	};

private:
	static void RefreshDebuggedObjects(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& InTracks, TArray<TSharedPtr<FDebugObjectInfo>>& OutObjects);

	void OnTrackListChanged();

	void OnPIEStarted(bool bSimulating);
	void OnPIEPaused(bool bSimulating);
	void OnPIEResumed(bool bSimulating);
	void OnPIEStopped(bool bSimulating);
	void OnPIESingleStepped(bool bSimulating);

	FDelegateHandle PreBeginPIEHandle;
	FDelegateHandle PausePIEHandle;
	FDelegateHandle ResumePIEHandle;
	FDelegateHandle SingleStepPIEHandle;
	FDelegateHandle ShutdownPIEHandle;

	void SetCurrentScrubTime(double Time);

	TBindableProperty<double> TraceTime;
	TBindableProperty<double> RecordingDuration;
	TBindableProperty<FString, BindingType_Out> RootObjectName;

	enum class EControlState : int8
	{
		Play,
		PlayReverse,
		Pause
	};

	EControlState ControlState = EControlState::Pause;

	FOnTrackListChanged TrackListChangedDelegate;
	FOnTrackCursor TrackCursorDelegate;

	bool bTraceJustConnected = false;
	bool bPIEStarted = false;
	bool bPIESimulating = false;

	bool bRecording = false;

	double PreviousTraceTime = -1;
	double CurrentScrubTime = 0;
	TRange<double> CurrentViewRange{ 0, 10 };
	TRange<double> CurrentTraceRange{ 0, 0 };
	uint16 RecordingIndex = 0;

	struct FScrubTimeInformation
	{
		/** Profiling/Tracing time */
		double ProfileTime = 0;
		/** Scrub Frame Index */
		int64 FrameIndex = 0;
	};

	FScrubTimeInformation ScrubTimeInformation;
	FScrubTimeInformation LowerBoundViewTimeInformation;
	FScrubTimeInformation UpperBoundViewTimeInformation;

	static void GetScrubTimeInformation(double InDebugTime, FScrubTimeInformation& InOutTimeInformation, uint16 InRecordingIndex, const TraceServices::IAnalysisSession* AnalysisSession);

	TArray<TSharedPtr<FDebugObjectInfo>> DebuggedObjects;
	mutable TSharedPtr<FDebugObjectInfo> SelectedObject;

	TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> DebugTracks;
	TSharedPtr<RewindDebugger::FRewindDebuggerTrack> SelectedTrack;

	TArray<RewindDebugger::FObjectId> CandidateIds;

	mutable class IUnrealInsightsModule* UnrealInsightsModule = nullptr;
	FTSTicker::FDelegateHandle TickerHandle;

	TOptional<FVector> RootObjectPosition;

	TArray<RewindDebugger::FRewindDebuggerTrackType> TrackTypes;

	bool bIsDetailsPanelOpen = true;

	uint64 DisplayWorldId = 0;
	bool bDisplayWorldIdValid = false;
};
