// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SMetaHumanSingleImageViewer.h"
#include "Widgets/SMetaHumanImageViewerScrubber.h"
#include "Widgets/SMetaHumanCalibrationObjectWidget.h"

#include "Style/MetaHumanCalibrationStyle.h"

#include "UMetaHumanRobustFeatureMatcher.h"
#include "MetaHumanCalibrationDiagnosticsOptions.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SCalibrationDiagnosticsImageViewer.h"

#include "Framework/Docking/WorkspaceItem.h"

class SCalibrationDiagnosticsWindow : public SCompoundWidget, public IFeatureDetector
{
public:
	SLATE_BEGIN_ARGS(SCalibrationDiagnosticsWindow)
		: _FootageCaptureData(nullptr)
		{
		}
		SLATE_ARGUMENT(UFootageCaptureData*, FootageCaptureData)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& InOwningWindow, const TSharedRef<class SDockTab>& InOwningTab);

	void SetImages(int32 InFrameId);

	void OnClose();

private:

	void RegisterImageViewerTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem);
	void RegisterOptionsTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem);

	virtual FDetectedFeatures GetDetectedFeatures(int32 InFrameId) override;
	virtual FDetectedFeatures DetectFeatures(int32 InFrameId) override;

	void OnFinishedChangingProperties(const FPropertyChangedEvent& InPropertyChangedEvent);

	TStrongObjectPtr<UFootageCaptureData> CaptureData;
	TStrongObjectPtr<UMetaHumanCalibrationDiagnosticsOptions> Options;
	TStrongObjectPtr<UMetaHumanRobustFeatureMatcher> FeatureMatcher;

	TSharedPtr<SCalibrationDiagnosticsImageViewer> ImageViewer;
	using SMetaHumanCalibrationOptionsWidget = SMetaHumanCalibrationObjectWidget<UMetaHumanCalibrationDiagnosticsOptions>;
	TSharedPtr<SMetaHumanCalibrationOptionsWidget> OptionsViewer;

	bool bIsFeatureDetectorInitialized = false;

	TSharedPtr<FTabManager> TabManager;
};