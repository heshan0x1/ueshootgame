// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCalibrationGeneratorOptions.h"
#include "MetaHumanCalibrationGeneratorConfig.h"

#include "MetaHumanCalibrationGeneratorState.h"
#include "MetaHumanCalibrationGenerator.h"

#include "Style/MetaHumanCalibrationStyle.h"

#include "CaptureData.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"

#include "Framework/Docking/WorkspaceItem.h"

#include "Widgets/SMetaHumanCalibrationImageViewer.h"
#include "Widgets/SMetaHumanCalibrationObjectWidget.h"

#include "SMetaHumanCalibrationGeneratorWindow.generated.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibration"

class FMetaHumanCalibrationWindowCommands
	: public TCommands<FMetaHumanCalibrationWindowCommands>
{
public:

	/** Default constructor. */
	FMetaHumanCalibrationWindowCommands()
		: TCommands<FMetaHumanCalibrationWindowCommands>(
			"MetaHumanCalibration",
			NSLOCTEXT("Contexts", "MetaHumanCalibration", "MetaHuman Calibration"),
			NAME_None, FAppStyle::GetAppStyleSetName()
		)
	{
	}

public:

	//~ TCommands interface
	virtual void RegisterCommands() override
	{
		UI_COMMAND(OpenConfig, "Open Config", "Open an existing config", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(SaveConfig, "Save Config", "Save config", EUserInterfaceActionType::Button, FInputChord());

		UI_COMMAND(RunCalibration, "Run Calibration", "Runs the calibration process", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(RunAutoFrameSelection, "Run Auto Frame Selection", "Runs the automatic frame selection", EUserInterfaceActionType::Button, FInputChord());

		UI_COMMAND(OpenSettings, "Project Settings", "Opens project settings for the calibration generator", EUserInterfaceActionType::Button, FInputChord());
	}

public:

	/** Open an existing config. */
	TSharedPtr<FUICommandInfo> OpenConfig;

	/** Save config. */
	TSharedPtr<FUICommandInfo> SaveConfig;

	/** Run calibration. */
	TSharedPtr<FUICommandInfo> RunCalibration;

	/** Run automatic frame selection. */
	TSharedPtr<FUICommandInfo> RunAutoFrameSelection;

	/** Open project settings. */
	TSharedPtr<FUICommandInfo> OpenSettings;
};

UCLASS()
class UMetaHumanCalibrationMenuContext : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<class SMetaHumanCalibrationGeneratorWindow> Widget;
};

class SMetaHumanCalibrationGeneratorWindow : 
	public SCompoundWidget
{
public:

	static const FName CalibrationMainMenuName;

	SMetaHumanCalibrationGeneratorWindow();

	SLATE_BEGIN_ARGS(SMetaHumanCalibrationGeneratorWindow)
		: _CaptureData(nullptr)
		{
		}

		SLATE_ARGUMENT(UFootageCaptureData*, CaptureData)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& OwningWindow, const TSharedRef<class SDockTab>& OwningTab);

	void OnClose();

private:
	void ResetState();

	FString GetDefaultPackagePath() const;
	FString GetDefaultAssetName() const;
	void OnFrameSelectionChanged(int32 InFrame);

	void RegisterToolbar(class UToolMenu* InToolbarMenu);
	TSharedPtr<SWidget> GenerateToolbarWidget(class UToolMenu* InToolbarMenu);
	void RegisterCommandHandlers();

	void SetConfig(UMetaHumanCalibrationGeneratorConfig* InConfig);
	void OpenConfig();
	void SaveConfig();
	void RunCalibration();
	void ShowCalibrationRMS();
	void RunAutomaticFrameSelection();
	void OpenSettings();

	FMetaHumanCalibrationPatternDetector::FDetectedFrame DetectForFrame(int32 InFrame);
	FMetaHumanCalibrationPatternDetector::FDetectedFrame DetectForFrame(const TMap<FString, FIntVector2>& InCameras,
																		const TPair<FString, FString>& InPath);

	void OnDetailsPropertyChanged(const FPropertyChangedEvent& InPropertyChangedEvent);

	void RegisterImageViewerTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem);
	void RegisterOptionsTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem);
	void RegisterConfigTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	using SMetaHumanCalibrationConfigWidget = SMetaHumanCalibrationObjectWidget<UMetaHumanCalibrationGeneratorConfig>;
	using SMetaHumanCalibrationOptionsWidget = SMetaHumanCalibrationObjectWidget<UMetaHumanCalibrationGeneratorOptions>;

	TSharedPtr<SMetaHumanCalibrationImageViewer> ImageViewer;
	TSharedPtr<SMetaHumanCalibrationOptionsWidget> OptionsTab;
	TSharedPtr<SMetaHumanCalibrationConfigWidget> ConfigTab;
	TSharedPtr<SWidget> Toolbar;

	TStrongObjectPtr<UFootageCaptureData> CaptureData;

	TSharedRef<FUICommandList> ToolkitUICommandList;
	TSharedPtr<FTabManager> TabManager;

	TStrongObjectPtr<UMetaHumanCalibrationGenerator> Engine;
	TSharedPtr<FMetaHumanCalibrationGeneratorState> State;
};

#undef LOCTEXT_NAMESPACE