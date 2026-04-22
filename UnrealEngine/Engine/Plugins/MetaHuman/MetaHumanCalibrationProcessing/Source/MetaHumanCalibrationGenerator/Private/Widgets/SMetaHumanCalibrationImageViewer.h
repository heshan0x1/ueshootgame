// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Style/MetaHumanCalibrationStyle.h"

#include "MetaHumanCalibrationGenerator.h"
#include "MetaHumanCalibrationPatternDetector.h"
#include "MetaHumanCalibrationGeneratorState.h"
#include "Utils/MetaHumanChessboardPointCounter.h"
#include "Utils/MetaHumanCalibrationAreaSelection.h"

#include "Widgets/SMetaHumanImageViewerScrubber.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SMetaHumanSingleImageViewer.h"

#include "CaptureData.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationViewer"

class FMetaHumanCalibrationViewCommands
	: public TCommands<FMetaHumanCalibrationViewCommands>
{
public:

	/** Default constructor. */
	FMetaHumanCalibrationViewCommands()
		: TCommands<FMetaHumanCalibrationViewCommands>(
			"MetaHumanCalibrationView",
			NSLOCTEXT("Contexts", "MetaHumanCalibrationView", "MetaHuman Calibration View"),
			NAME_None, FAppStyle::GetAppStyleSetName()
		)
	{
	}

public:

	//~ TCommands interface
	virtual void RegisterCommands() override
	{
		UI_COMMAND(SelectAreaOfInterest, "Select Area of Interest", "Selects area of interest for a view", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ResetView, "Reset View", "Resets the view to default", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ToggleCoverage, "Toggle Coverage", "Toggle coverage of the selected frames", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleDetectedPoints, "Toggle Detected Points", "Toggle detect points for the current frame", EUserInterfaceActionType::ToggleButton, FInputChord());
		UI_COMMAND(ToggleAreaOfInterest, "Toggle Area of Interest", "Toggle the area of interest rectangle", EUserInterfaceActionType::ToggleButton, FInputChord());
	}

public:

	/** Enters area of interest selection mode. */
	TSharedPtr<FUICommandInfo> SelectAreaOfInterest;

	/** Resets view. */
	TSharedPtr<FUICommandInfo> ResetView;

	/** Toggle coverage. */
	TSharedPtr<FUICommandInfo> ToggleCoverage;

	/** Toggle detected points and lines. */
	TSharedPtr<FUICommandInfo> ToggleDetectedPoints;

	/** Toggle the area of interest rectangle */
	TSharedPtr<FUICommandInfo> ToggleAreaOfInterest;
};

class SMetaHumanCalibrationImageViewer : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam(FFrameSelected, int32);

	using FPairString = TPair<FString, FString>;
	using FPairArray = TPair<TArray<FString>, TArray<FString>>;
	using FPairVector = TPair<FIntVector2, FIntVector2>;
	DECLARE_DELEGATE_RetVal_OneParam(FMetaHumanCalibrationPatternDetector::FDetectedFrame, FDetectForFrame, int32)

	SLATE_BEGIN_ARGS(SMetaHumanCalibrationImageViewer)
		: _FootageCaptureData(nullptr)
		{
		}

		SLATE_ARGUMENT(UFootageCaptureData*, FootageCaptureData)
		SLATE_ARGUMENT(TWeakPtr<FMetaHumanCalibrationGeneratorState>, State)

		SLATE_EVENT(FFrameSelected, FrameSelected)
		SLATE_EVENT(FDetectForFrame, DetectForFrame)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& OwningWindow, const TSharedRef<class SDockTab>& OwningTab);
	void OnClose();

	void ResetView();
	void ResetState();

	void SelectCurrentFrame();
	void NextFrame(int32 InStep = 1);
	void PreviousFrame(int32 InStep = 1);

	void SetDetectedPointsForFrame(int32 InFrame, FMetaHumanCalibrationPatternDetector::FDetectedFrame InDetectedFrame);

	FPairString GetCameraNames() const;
	FPairVector GetFrameDimensions() const;
	FPairString GetFramePath(int32 InFrame) const;
	FPairArray GetFramePaths() const;
private:

	void InvalidateCounter();

	void AddOverlay(FBox2d InUVRegion, const FGeometry& InAllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);
	void ShowCoverage(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
					  FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);
	void ShowSingleFrameDetectedPoints(const FMetaHumanCalibrationPatternDetector::FDetectedFrame& InDetectedFrame,
									   FBox2d InUVRegion, const FGeometry& InAllottedGeometry, 
									   FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);
	void ShowAreaOfInterest(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
							FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);

	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);
	
	void UpdateImages(const int32 InFrameNumber);

	void OnScrubberValueChanged(float InValue);

	FReply OnFrameSelectedClicked();
	void SelectFrame(int32 InFrame);
	FMetaHumanCalibrationPatternDetector::FDetectedFrame RunDetectForFrame(int32 InFrame);
	const FSlateBrush* GetSelectFrameButtonImage() const;

	FReply OnNextSelectedFrameClicked();
	FReply OnPreviousSelectedFrameClicked();
	bool IsPreviousNextButtonEnabled() const;
	FReply OnResetButtonClicked();

	TSharedPtr<SWidget> GetViewToolbarWidget(int32 InCameraIndex);
	void RegisterCommandHandlers();

	enum class EViewOptions : uint8
	{
		Nothing = 0,
		Coverage = 1 << 0,
		DetectedPoints = 1 << 1,
		AreaOfInterest = 1 << 2,

		All = Coverage | DetectedPoints | AreaOfInterest
	};

	void StartAreaOfInterestSelection(int32 InCameraIndex);
	void UpdateAfterSelection(FSlateRect InSlateRect, FBox2d InUvRegion, FVector2D InWidgetSize, int32 InCameraIndex);
	void ResetView(int32 InCameraIndex);
	void ToggleView(int32 InIndex, EViewOptions InView);
	ECheckBoxState IsViewToggled(int32 InIndex, EViewOptions InView) const;

	TSharedRef<SWidget> FillDisplayOptionsForViewMenu(int32 InCameraIndex);

	TWeakObjectPtr<UFootageCaptureData> CaptureData;
	TWeakPtr<FMetaHumanCalibrationGeneratorState> State;

	TArray<TSharedPtr<SMetaHumanCalibrationSingleImageViewer>> SingleImageViewers;
	TMap<int32, FMetaHumanCalibrationPatternDetector::FDetectedFrame> DetectedPointsForSelectedFrames;

	TSharedPtr<SMetaHumanImageViewerScrubber> ScrubberSlider;

	TOptional<int32> PendingValueChange;
	FFrameSelected FrameSelected;
	FDetectForFrame DetectForFrame;

	TUniquePtr<FMetaHumanChessboardPointCounter> ChessboardPointCounter;
	TMap<int32, TSharedRef<FUICommandList>> CameraToolkitCommands;

	FRIEND_ENUM_CLASS_FLAGS(EViewOptions);

	TMap<int32, EViewOptions> ShowViewOptions;
};

#undef LOCTEXT_NAMESPACE