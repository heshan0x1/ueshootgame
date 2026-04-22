// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SMetaHumanSingleImageViewer.h"
#include "Widgets/SMetaHumanImageViewerScrubber.h"
#include "MetaHumanCalibrationDiagnosticsCommands.h"

#include "UMetaHumanRobustFeatureMatcher.h"
#include "MetaHumanCalibrationDiagnosticsOptions.h"

#include "Utils/MetaHumanCalibrationErrorCalculator.h"

#include "CaptureData.h"
#include "Style/MetaHumanCalibrationStyle.h"

class IFeatureDetector
{
public:

	virtual ~IFeatureDetector() = default;

	virtual FDetectedFeatures GetDetectedFeatures(int32 InFrameId) = 0;
	virtual FDetectedFeatures DetectFeatures(int32 InFrameId) = 0;
};

class SCalibrationDiagnosticsImageViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCalibrationDiagnosticsImageViewer)
		: _FootageCaptureData(nullptr)
		, _Options(nullptr)
		, _FeatureDetector()
		{
		}
		SLATE_ARGUMENT(UFootageCaptureData*, FootageCaptureData)
		SLATE_ARGUMENT(UMetaHumanCalibrationDiagnosticsOptions*, Options)
		SLATE_ARGUMENT(TSharedPtr<IFeatureDetector>, FeatureDetector)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetImages(int32 InFrameId);

	void UpdateState();
	void ResetState();
	void OnClose();

private:

	void OnAddOverlays(FBox2d InUvRegion,
					   const FGeometry& InAllottedGeometry,
					   FSlateWindowElementList& OutDrawElements,
					   int32& OutLayerId,
					   int32 InCameraIndex);
	void OnImageClicked(FVector2D InMousePoint, FBox2d InUvRegion, FVector2D InWidgetSize, int32 InCameraIndex);

	void ShowDetectedFeatures(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
							  FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);
	void ShowAreaOfInterest(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
							FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);
	void ShowGridMap(FBox2d InUVRegion, const FGeometry& InAllottedGeometry,
					 FSlateWindowElementList& OutDrawElements, int32& OutLayerId, int32 InCameraIndex);

	EVisibility ImagesNotFoundVisibility() const;

	void UpdateErrors();
	void CalculateErrors(int32 InFrameIndex, bool bInUpdateUI = true);

	void Tick(const FGeometry& InAllottedGeometry, const double InCurrentTime, const float InDeltaTime);

	void OnScrubberValueChanged(float InValue);

	FText HandleDetectButtonText() const;
	FReply OnDetectButtonClicked();
	bool DetectForFrame(int32 InFrame);
	bool IsDetectButtonEnabled() const;
	FText HandleMeanErrorTextBlock() const;
	FText HandleRMSErrorTextBlock() const;
	FSlateColor HandleRMSErrorColor() const;
	FText HandleErrorTextBlock(double InError) const;
	FSlateColor HandleErrorColor(double InError, double InThreshold) const;

	TSharedPtr<SWidget> GetViewToolbarWidget(int32 InCameraIndex);
	TSharedRef<SWidget> FillDisplayOptionsForViewMenu(int32 InCameraIndex);
	void RegisterCommandHandlers();

	enum class EViewOptions : uint8
	{
		Nothing = 0,
		DetectedFeatures = 1 << 0,
		AreaOfInterests = 1 << 1,
		ErrorsPerBlock = 1 << 2,

		All = DetectedFeatures | AreaOfInterests | ErrorsPerBlock
	};

	FRIEND_ENUM_CLASS_FLAGS(EViewOptions);

	void StartAreaOfInterestSelection(int32 InCameraIndex);
	void UpdateAfterSelection(FSlateRect InSlateRect, FBox2d InUvRegion, FVector2D InWidgetSize, int32 InCameraIndex);
	void ResetView(int32 InCameraIndex);
	void ToggleView(int32 InIndex, EViewOptions InView);
	ECheckBoxState IsViewToggled(int32 InIndex, EViewOptions InView) const;

	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	TArray<TSharedPtr<SMetaHumanCalibrationSingleImageViewer>> ImageViewers;
	TSharedPtr<SMetaHumanImageViewerScrubber> ScrubberSlider;

	int32 CurrentFrameId;
	TOptional<int32> PendingValueChange;

	TArray<int32> DetectedFrames;

	TUniquePtr<FMetaHumanCalibrationErrorCalculator> Calculator;

	TMap<int32, TSharedRef<FUICommandList>> CameraToolkitCommands;
	TMap<int32, EViewOptions> ShowViewOptions;

	TWeakObjectPtr<UFootageCaptureData> CaptureData;
	TWeakObjectPtr<UMetaHumanCalibrationDiagnosticsOptions> Options;
	TSharedPtr<IFeatureDetector> FeatureDetector;

	TArray<int32> SelectedBlockId;

	TOptional<double> CurrentFrameRMS;
	TOptional<double> CurrentFrameMean;
};