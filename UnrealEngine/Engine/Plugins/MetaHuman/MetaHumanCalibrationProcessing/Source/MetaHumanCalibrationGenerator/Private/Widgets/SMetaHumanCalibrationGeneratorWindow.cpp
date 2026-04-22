// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCalibrationGeneratorWindow.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#include "Async/Async.h"
#include "Tasks/Task.h"
#include "Async/Fundamental/Task.h"

#include "ImageUtils.h"
#include "ImgMediaSource.h"
#include "ImageSequenceUtils.h"

#include "Engine/Texture2D.h"

#include "JsonObjectConverter.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"

#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/LayoutService.h"

#include "Interfaces/IMainFrameModule.h"

#include "ParseTakeUtils.h"
#include "Utils/MetaHumanCalibrationUtils.h"
#include "ISettingsModule.h"
#include "Settings/MetaHumanCalibrationGeneratorSettings.h"

#include "Utils/MetaHumanCalibrationAutoFrameSelection.h"

#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCalibrationGeneratorWindow"

namespace UE::MetaHuman::Private
{

TSharedRef<FJsonObject> ConfigToJson(const UMetaHumanCalibrationGeneratorConfig* InConfig)
{
	TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(UMetaHumanCalibrationGeneratorConfig::StaticClass(), InConfig, JsonObject);

	return JsonObject;
}

UMetaHumanCalibrationGeneratorConfig* JsonToConfig(TSharedRef<FJsonObject> InJsonObject)
{
	UMetaHumanCalibrationGeneratorConfig* OutConfigData = NewObject<UMetaHumanCalibrationGeneratorConfig>(GetTransientPackage());
	FJsonObjectConverter::JsonObjectToUStruct(InJsonObject, UMetaHumanCalibrationGeneratorConfig::StaticClass(), OutConfigData);

	return OutConfigData;
}

FString ConfigToString(const UMetaHumanCalibrationGeneratorConfig* InConfig)
{
	FString JsonObject;

	FJsonObjectConverter::UStructToJsonObjectString(UMetaHumanCalibrationGeneratorConfig::StaticClass(), InConfig, JsonObject, 0, 0, 0, nullptr, false);

	return JsonObject;
}

UMetaHumanCalibrationGeneratorConfig* StringToConfig(const FString& InConfig)
{
	UMetaHumanCalibrationGeneratorConfig* OutConfigData = NewObject<UMetaHumanCalibrationGeneratorConfig>(GetTransientPackage());

	FJsonObjectConverter::JsonObjectStringToUStruct(InConfig, OutConfigData);

	return OutConfigData;
}

void SaveConfig(const UMetaHumanCalibrationGeneratorConfig* InConfig, const FString& InPath)
{
	const TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileWriter(*InPath));
	if (Archive)
	{
		const TSharedPtr<FJsonObject> JsonObject = UE::MetaHuman::Private::ConfigToJson(InConfig);

		const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(Archive.Get(), 0);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

		ensure(Archive->Close());
	}
}

UMetaHumanCalibrationGeneratorConfig* OpenConfig(const FString& InPath)
{
	if (IFileManager::Get().FileExists(*InPath))
	{
		const TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(*InPath));
		if (Archive)
		{
			const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Archive.Get());

			TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
			if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
			{
				return JsonToConfig(JsonObject.ToSharedRef());
			}
		}
	}

	return nullptr;
}

static const FName ImageViewerTabName("ImageViewerTab");
static const FName OptionsTabName("OptionsTab");
static const FName ConfigTabName("ConfigTab");

}

const FName SMetaHumanCalibrationGeneratorWindow::CalibrationMainMenuName("MetaHuman.Calibration.Toolbar");

SMetaHumanCalibrationGeneratorWindow::SMetaHumanCalibrationGeneratorWindow()
	: CaptureData(nullptr)
	, ToolkitUICommandList(MakeShared<FUICommandList>())
{
}

void SMetaHumanCalibrationGeneratorWindow::Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& OwningWindow, const TSharedRef<class SDockTab>& OwningTab)
{
	CaptureData = TStrongObjectPtr<UFootageCaptureData>(InArgs._CaptureData);

	Engine.Reset(NewObject<UMetaHumanCalibrationGenerator>());
	State = MakeShared<FMetaHumanCalibrationGeneratorState>();

	FString LastConfig = GetDefault<UMetaHumanCalibrationGeneratorSettings>()->LastConfigUsed;
	if (LastConfig.IsEmpty())
	{
		State->Config.Reset(NewObject<UMetaHumanCalibrationGeneratorConfig>());
	}
	else
	{
		State->Config.Reset(UE::MetaHuman::Private::StringToConfig(LastConfig));
	}

	State->Options.Reset(NewObject<UMetaHumanCalibrationGeneratorOptions>());

	Engine->Init(State->Config.Get());

	TabManager = FGlobalTabmanager::Get()->NewTabManager(OwningTab);

	const TSharedRef<FWorkspaceItem> TargetSetsWorkspaceMenuCategory = 
		TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("CalibrationGeneratorWorkspaceMenuCategory", "Calibration Generator"));
	RegisterImageViewerTabSpawner(TabManager.ToSharedRef(), TargetSetsWorkspaceMenuCategory);
	RegisterOptionsTabSpawner(TabManager.ToSharedRef(), TargetSetsWorkspaceMenuCategory);
	RegisterConfigTabSpawner(TabManager.ToSharedRef(), TargetSetsWorkspaceMenuCategory);

	const auto& PersistLayout = [](const TSharedRef<FTabManager::FLayout>& LayoutToSave)
		{
			FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, LayoutToSave);
		};

	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateLambda(PersistLayout));

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("CalibrationGenerator")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(false)
				->SetSizeCoefficient(0.75)
				->AddTab(UE::MetaHuman::Private::ImageViewerTabName, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.25)
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5)
					->SetHideTabWell(false)
					->AddTab(UE::MetaHuman::Private::ConfigTabName, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5)
					->SetHideTabWell(false)
					->AddTab(UE::MetaHuman::Private::OptionsTabName, ETabState::OpenedTab)
				)
			)	
		);

	FMetaHumanCalibrationWindowCommands::Register();

	RegisterCommandHandlers();

	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* ToolbarMenu = ToolMenus->FindMenu(CalibrationMainMenuName);
	if (!ToolbarMenu)
	{
		// Automatically deleted when UE shuts down
		ToolbarMenu = ToolMenus->RegisterMenu(CalibrationMainMenuName,
											  NAME_None,
											  EMultiBoxType::SlimHorizontalToolBar);

	}
	check(ToolbarMenu);

	ToolbarMenu->bToolBarForceSmallIcons = false;
	ToolbarMenu->bToolBarIsFocusable = false;

	RegisterToolbar(ToolbarMenu);
	Toolbar = GenerateToolbarWidget(ToolbarMenu);

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0, 0.0, 0.0, 4.0)
			[
				Toolbar.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				TabManager->RestoreFrom(Layout, OwningWindow).ToSharedRef()
			]
		];

	ResetState();
}

void SMetaHumanCalibrationGeneratorWindow::ResetState()
{
	Engine->ConfigureCameras(CaptureData.Get());

	State->Options->AssetName = GetDefaultAssetName();
	State->Options->PackagePath.Path = GetDefaultPackagePath();

	OptionsTab->Refresh();
}

void SMetaHumanCalibrationGeneratorWindow::OnClose()
{
	ImageViewer->OnClose();

	FMetaHumanCalibrationWindowCommands::Unregister();
}

FString SMetaHumanCalibrationGeneratorWindow::GetDefaultPackagePath() const
{
	const FString PackagePath = FPaths::GetPath(CaptureData->GetOuter()->GetName());
	return PackagePath;
}

FString SMetaHumanCalibrationGeneratorWindow::GetDefaultAssetName() const
{
	const FString AssetName = TEXT("CC_") + CaptureData->GetName();
	return AssetName;
}

void SMetaHumanCalibrationGeneratorWindow::OnFrameSelectionChanged(int32 InFrame)
{
	OptionsTab->Refresh();
}

void SMetaHumanCalibrationGeneratorWindow::RegisterToolbar(UToolMenu* InToolbarMenu)
{
	FMetaHumanCalibrationWindowCommands& Commands = FMetaHumanCalibrationWindowCommands::Get();

	FToolMenuSection& ConfigSection = InToolbarMenu->AddSection(TEXT("Config"));
	{
		ConfigSection.AddEntry(FToolMenuEntry::InitComboButton(
			"Config",
			FUIAction(),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* InNewToolMenu)
			{
				FMetaHumanCalibrationWindowCommands& Commands = FMetaHumanCalibrationWindowCommands::Get();
				FToolMenuSection& ConfigSection = InNewToolMenu->AddSection(TEXT("Config"));

				ConfigSection.AddEntry(FToolMenuEntry::InitMenuEntry(
					Commands.OpenConfig,
					Commands.OpenConfig->GetLabel(),
					Commands.OpenConfig->GetDescription(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.OpenCurrentProjectDirectory")
				));

				ConfigSection.AddEntry(FToolMenuEntry::InitMenuEntry(
					Commands.SaveConfig,
					Commands.SaveConfig->GetLabel(),
					Commands.SaveConfig->GetDescription(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "MainFrame.AutoSaveImage")
				));
			}),
			LOCTEXT("CalibrationGenerator_ConfigLabel", "Config"),
			LOCTEXT("CalibrationGenerator_ConfigTooltip", "Opens config commands"),
			FSlateIcon(FMetaHumanCalibrationStyle::Get().GetStyleSetName(), "MetaHumanCalibration.Generator.Config")
		));

		ConfigSection.AddSeparator(NAME_None);
	}

	FToolMenuSection& ProcessSection = InToolbarMenu->AddSection(TEXT("Process"));
	{
		ProcessSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.RunAutoFrameSelection,
			Commands.RunAutoFrameSelection->GetLabel(),
			Commands.RunAutoFrameSelection->GetDescription(),
			FSlateIcon(FMetaHumanCalibrationStyle::Get().GetStyleSetName(), "MetaHumanCalibration.Generator.AutoFrameSelection")
		));

		ProcessSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.RunCalibration,
			Commands.RunCalibration->GetLabel(),
			Commands.RunCalibration->GetDescription(),
			FSlateIcon(FMetaHumanCalibrationStyle::Get().GetStyleSetName(), "MetaHumanCalibration.Generator.Run")
		));

		ProcessSection.AddSeparator(NAME_None);
	}

	if (FGlobalTabmanager::Get()->GetTabPermissionList()->PassesFilter("ProjectSettings"))
	{
		FToolMenuSection& SettingsSection = InToolbarMenu->AddSection(TEXT("Settings"));
		{
			SettingsSection.AddEntry(FToolMenuEntry::InitToolBarButton(
				Commands.OpenSettings,
				Commands.OpenSettings->GetLabel(),
				Commands.OpenSettings->GetDescription(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "ProjectSettings.TabIcon")
			));

			SettingsSection.Alignment = EToolMenuSectionAlign::Last;
		}
	}
}

TSharedPtr<SWidget> SMetaHumanCalibrationGeneratorWindow::GenerateToolbarWidget(UToolMenu* InToolbarMenu)
{
	FToolMenuContext MenuContext(ToolkitUICommandList);
	UMetaHumanCalibrationMenuContext* ContextObject = NewObject<UMetaHumanCalibrationMenuContext>();
	ContextObject->Widget = SharedThis(this);
	MenuContext.AddObject(ContextObject);

	UToolMenu* GeneratedMenu = UToolMenus::Get()->GenerateMenu(CalibrationMainMenuName, MenuContext);
	
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0)
		[
			UToolMenus::Get()->GenerateWidget(GeneratedMenu)
		];
}

void SMetaHumanCalibrationGeneratorWindow::RegisterCommandHandlers()
{
	FMetaHumanCalibrationWindowCommands& Commands = FMetaHumanCalibrationWindowCommands::Get();

	ToolkitUICommandList->MapAction(Commands.OpenConfig,
									FExecuteAction::CreateSP(this, &SMetaHumanCalibrationGeneratorWindow::OpenConfig), nullptr);
	ToolkitUICommandList->MapAction(Commands.SaveConfig,
									FExecuteAction::CreateSP(this, &SMetaHumanCalibrationGeneratorWindow::SaveConfig), nullptr);

	ToolkitUICommandList->MapAction(Commands.RunAutoFrameSelection,
									FExecuteAction::CreateSP(this, &SMetaHumanCalibrationGeneratorWindow::RunAutomaticFrameSelection), nullptr);
	ToolkitUICommandList->MapAction(Commands.RunCalibration,
									FExecuteAction::CreateSP(this, &SMetaHumanCalibrationGeneratorWindow::RunCalibration), nullptr);

	ToolkitUICommandList->MapAction(Commands.OpenSettings,
									FExecuteAction::CreateSP(this, &SMetaHumanCalibrationGeneratorWindow::OpenSettings), nullptr);
}

void SMetaHumanCalibrationGeneratorWindow::SetConfig(UMetaHumanCalibrationGeneratorConfig* InConfig)
{
	State->Config.Reset(InConfig);

	if (CaptureData)
	{
		Engine->Reset(InConfig, CaptureData.Get());
	}
	else
	{
		Engine->Init(InConfig);
	}

	if (ConfigTab)
	{
		ConfigTab->Reset(InConfig);
	}
}

void SMetaHumanCalibrationGeneratorWindow::OpenConfig()
{
	TArray<FString> OpenFileNames;

	const FString FileDescription = TEXT("MetaHuman Calibration Generator Config");
	const FString Extensions = TEXT("json");
	const FString FileTypes = FString::Printf(TEXT("%s (*.%s)|*.%s"), *FileDescription, *Extensions, *Extensions);

	const FString DefaultFile = TEXT("MetaHumanCalibrationConfig");

	FString DefaultOpenPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN);

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bFileSelected = DesktopPlatform->OpenFileDialog(
		ParentWindowWindowHandle,
		LOCTEXT("MetaHumanCalibrationOpenTitle", "Open").ToString(),
		DefaultOpenPath,
		DefaultFile,
		FileTypes,
		EFileDialogFlags::None,
		OpenFileNames);

	if (bFileSelected && OpenFileNames.Num() > 0)
	{
		UMetaHumanCalibrationGeneratorConfig* NewConfig = UE::MetaHuman::Private::OpenConfig(OpenFileNames[0]);
		if (NewConfig)
		{
			SetConfig(NewConfig);
		}
	}
}

void SMetaHumanCalibrationGeneratorWindow::SaveConfig()
{
	const FString FileDescription = TEXT("MetaHuman Calibration Generator Config");
	const FString Extensions = TEXT("json");
	const FString FileTypes = FString::Printf(TEXT("%s (*.%s)|*.%s"), *FileDescription, *Extensions, *Extensions);

	const FString DefaultFile = TEXT("MetaHumanCalibrationConfig");

	TArray<FString> SaveFileNames;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	const void* ParentWindowWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	const bool bFileSelected = DesktopPlatform->SaveFileDialog(
		ParentWindowWindowHandle,
		LOCTEXT("MetaHumanCalibrationSaveAsTitle", "Save As").ToString(),
		FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_SAVE),
		DefaultFile,
		FileTypes,
		EFileDialogFlags::None,
		SaveFileNames);

	if (bFileSelected && SaveFileNames.Num() > 0)
	{
		UE::MetaHuman::Private::SaveConfig(State->Config.Get(), SaveFileNames[0]);
	}
}

void SMetaHumanCalibrationGeneratorWindow::RunCalibration()
{
	if (State->Options->SelectedFrames.IsEmpty())
	{
		FTextBuilder TextBuilder;
		TextBuilder.AppendLine(LOCTEXT("RunCalibration_NoFramesWarning", "No frames are currently selected for the calibration process."));
		TextBuilder.AppendLine();
		TextBuilder.AppendLine(LOCTEXT("RunCalibration_Question", "Do you want to run the Automatic Frame Selection?"));

		EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgCategory::Warning,
														   EAppMsgType::YesNo,
														   TextBuilder.ToText(),
														   LOCTEXT("RunCalibration_WarningTitle", "No frames selected"));
		if (Result == EAppReturnType::Yes)
		{
			RunAutomaticFrameSelection();
		}
	}

	if (State->Options->SelectedFrames.IsEmpty())
	{
		return;
	}

	auto Task = [WeakPtr = AsWeakSubobject<SMetaHumanCalibrationGeneratorWindow>(this)]()
		{
			TSharedPtr<SMetaHumanCalibrationGeneratorWindow> This = WeakPtr.Pin();

			if (This)
			{
				bool bSuccess = This->Engine->Process(This->CaptureData.Get(), This->State->Options.Get());

				UMetaHumanCalibrationGeneratorSettings* Settings =
					GetMutableDefault<UMetaHumanCalibrationGeneratorSettings>();
				Settings->LastConfigUsed = UE::MetaHuman::Private::ConfigToString(This->State->Config.Get());
				Settings->SaveConfig();

				This->ShowCalibrationRMS();
			}
		};

	using namespace UE::Tasks;
	Launch(UE_SOURCE_LOCATION, MoveTemp(Task), ETaskPriority::BackgroundNormal);
}

void SMetaHumanCalibrationGeneratorWindow::ShowCalibrationRMS()
{
	ExecuteOnGameThread(TEXT("CalibrationSuccessDialog"),
			  [this]()
			  {
				  FTextBuilder TextBuilder;
				  TextBuilder.AppendLineFormat(
					  LOCTEXT("SuccessfullCalibration", "Calibration has completed with the RMS (Root Mean Squared) reprojection error of {0}."), Engine->GetLastRMSError());

				  FMessageDialog::Open(EAppMsgCategory::Success, EAppMsgType::Ok, TextBuilder.ToText());
			  });
}

void SMetaHumanCalibrationGeneratorWindow::RunAutomaticFrameSelection()
{
	using namespace UE::MetaHuman::Image;
	using FDetectedFrame = FMetaHumanCalibrationPatternDetector::FDetectedFrame;

	SMetaHumanCalibrationImageViewer::FPairArray FramePaths = ImageViewer->GetFramePaths();

	const UMetaHumanCalibrationGeneratorSettings* Settings =
		GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	TArray<int32> FilteredFrameIndices = FilterFrameIndices(FramePaths, [SampleRate = Settings->AutomaticFrameSelectionSampleRate](int32 InFrame)
	{
		return InFrame % SampleRate == 0;
	});

	TMap<int32, FDetectedFrame> DetectedFrames;

	FScopedSlowTask AutoFrameSelectionTask(FilteredFrameIndices.Num(), LOCTEXT("AutoFrameSelection_TaskMessage", "Running automatic frame selection process"));
	AutoFrameSelectionTask.MakeDialog(true);

	SMetaHumanCalibrationImageViewer::FPairString CameraNames = ImageViewer->GetCameraNames();
	SMetaHumanCalibrationImageViewer::FPairVector Dimensions = ImageViewer->GetFrameDimensions();
	TMap<FString, FIntVector2> Cameras;
	Cameras.Add(CameraNames.Key, Dimensions.Key);
	Cameras.Add(CameraNames.Value, Dimensions.Value);

	for (int32 FrameIndex : FilteredFrameIndices)
	{
		if (AutoFrameSelectionTask.ShouldCancel())
		{
			return;
		}

		TPair<FString, FString> FramePair = ImageViewer->GetFramePath(FrameIndex);

		TPromise<TPair<int32, FDetectedFrame>> Promise;
		TFuture<TPair<int32, FDetectedFrame>> Future = Promise.GetFuture();

		// Safe to capture this pointer because the Automatic Frame Selection process is a blocking process
		// Note: Only 1 task in the queue because we are waiting for it to finish before kicking of the next one
		auto Task = [this, &Promise, FramePair, Cameras, FrameIndex, FramePaths, FilteredFrameIndices]()
			{
				int32 FrameIndexToTry = FrameIndex;

				FDetectedFrame DetectedFrame = DetectForFrame(Cameras, FramePair);

				if (DetectedFrame.IsEmpty())
				{
					for (int32 TryAttempt = 1; TryAttempt < 5; ++TryAttempt)
					{
						FrameIndexToTry = FrameIndex + TryAttempt;

						if (!FramePaths.Key.IsValidIndex(FrameIndexToTry) ||
							!FramePaths.Value.IsValidIndex(FrameIndexToTry))
						{
							break;
						}

						if (FilteredFrameIndices.Contains(FrameIndexToTry))
						{
							break;
						}

						TPair<FString, FString> NewFramePair;
						NewFramePair.Key = FramePaths.Key[FrameIndexToTry];
						NewFramePair.Value = FramePaths.Value[FrameIndexToTry];

						DetectedFrame = DetectForFrame(Cameras, NewFramePair);

						if (!DetectedFrame.IsEmpty())
						{
							break;
						}
					}
				}

				Promise.SetValue({ FrameIndexToTry, DetectedFrame });
			};

		using namespace UE::Tasks;
		Launch(UE_SOURCE_LOCATION, MoveTemp(Task), ETaskPriority::BackgroundNormal);

		bool bCanceled = false;

		while (!Future.WaitFor(FTimespan::FromMilliseconds(50)))
		{
			if (AutoFrameSelectionTask.ShouldCancel())
			{
				bCanceled = true;
				break;
			}
		}

		TPair<int32, FDetectedFrame> DetectedFrame = Future.Get();

		if (!DetectedFrame.Value.IsEmpty())
		{
			DetectedFrames.Add(DetectedFrame.Key, MoveTemp(DetectedFrame.Value));
		}

		if (bCanceled)
		{
			return;
		}

		AutoFrameSelectionTask.EnterProgressFrame();
	}

	FMetaHumanCalibrationPatternDetector::FPatternInfo PatternInfo =
	{
		.Width = State->Config->BoardPatternWidth - 1,
		.Height = State->Config->BoardPatternHeight - 1,
		.SquareSize = State->Config->BoardSquareSize
	};

	TPair<FBox2D, FBox2D> AreaOfInterest =
	{
		State->Options->AreaOfInterestsForCameras[0].GetBox2D(), State->Options->AreaOfInterestsForCameras[1].GetBox2D()
	};

	FMetaHumanCalibrationAutoFrameSelection AutoFrameSelector(ImageViewer->GetCameraNames(), 
															  ImageViewer->GetFrameDimensions(),
															  MoveTemp(AreaOfInterest));

	TArray<int32> SelectedFrames = AutoFrameSelector.RunSelection(PatternInfo, DetectedFrames);
	SelectedFrames.Sort();

	for (int32 FrameIndex : SelectedFrames)
	{
		ImageViewer->SetDetectedPointsForFrame(FrameIndex, DetectedFrames[FrameIndex]);
	}

	const int32 NumberOfSelectedFrames = SelectedFrames.Num();

	if (NumberOfSelectedFrames < Settings->TargetNumberOfFrames)
	{
		FTextBuilder TextBuilder;
		TextBuilder.AppendLineFormat(
			LOCTEXT("InsufficientNumberOfFrames", "The number of frames ({0}) selected after automatic frame selection is lower than the specified Target Frames threshold of ({1})."), 
			NumberOfSelectedFrames, Settings->TargetNumberOfFrames);

		TextBuilder.AppendLine();
		TextBuilder.AppendLine(LOCTEXT("InsufficientNumberOfFramesAdvice", "Consider manually adding additional frames or decreasing the automatic frame selection sample rate"));

		FMessageDialog::Open(EAppMsgType::Ok, TextBuilder.ToText());
	}

	State->Options->SelectedFrames = MoveTemp(SelectedFrames);
}

void SMetaHumanCalibrationGeneratorWindow::OpenSettings()
{
	ISettingsModule& SettingsModule = FModuleManager::GetModuleChecked<ISettingsModule>("Settings");
	SettingsModule.ShowViewer("Project", "Plugins", "MetaHuman Calibration Generator");
}

FMetaHumanCalibrationPatternDetector::FDetectedFrame
SMetaHumanCalibrationGeneratorWindow::DetectForFrame(int32 InFrame)
{
	// Counting only the inner corners
	FMetaHumanCalibrationPatternDetector::FPatternInfo PatternInfo =
	{
		.Width = State->Config->BoardPatternWidth - 1,
		.Height = State->Config->BoardPatternHeight - 1,
		.SquareSize = State->Config->BoardSquareSize
	};

	SMetaHumanCalibrationImageViewer::FPairString CameraNames = ImageViewer->GetCameraNames();
	SMetaHumanCalibrationImageViewer::FPairVector Dimensions = ImageViewer->GetFrameDimensions();

	TMap<FString, FIntVector2> Cameras;
	Cameras.Add(CameraNames.Key, Dimensions.Key);
	Cameras.Add(CameraNames.Value, Dimensions.Value);

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromNew(PatternInfo, Cameras, GetDefault<UMetaHumanCalibrationGeneratorSettings>()->ScaleFactor);

	TOptional< FMetaHumanCalibrationPatternDetector::FDetectedFrame> FrameOpt =
		PatternDetector->DetectPattern(CameraNames, ImageViewer->GetFramePath(InFrame), State->Options->SharpnessThreshold);

	if (FrameOpt.IsSet())
	{
		return FrameOpt.GetValue();
	}

	return FMetaHumanCalibrationPatternDetector::FDetectedFrame();
}

FMetaHumanCalibrationPatternDetector::FDetectedFrame
SMetaHumanCalibrationGeneratorWindow::DetectForFrame(const TMap<FString, FIntVector2>& InCameras, const TPair<FString, FString>& InPath)
{
	FMetaHumanCalibrationPatternDetector::FPatternInfo PatternInfo =
	{
		.Width = State->Config->BoardPatternWidth - 1,
		.Height = State->Config->BoardPatternHeight - 1,
		.SquareSize = State->Config->BoardSquareSize
	};

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromNew(PatternInfo, InCameras, GetDefault<UMetaHumanCalibrationGeneratorSettings>()->ScaleFactor);

	TArray<FString> CameraNamesArray;
	InCameras.GetKeys(CameraNamesArray);

	check(InCameras.Num() == 2);

	TPair<FString, FString> CameraNames;
	CameraNames.Key = CameraNamesArray[0];
	CameraNames.Value = CameraNamesArray[1];

	TOptional< FMetaHumanCalibrationPatternDetector::FDetectedFrame> FrameOpt =
		PatternDetector->DetectPattern(CameraNames, InPath, State->Options->SharpnessThreshold);

	if (FrameOpt.IsSet())
	{
		return FrameOpt.GetValue();
	}

	return FMetaHumanCalibrationPatternDetector::FDetectedFrame();
}

void SMetaHumanCalibrationGeneratorWindow::OnDetailsPropertyChanged(const FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (InPropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationGeneratorConfig, BoardPatternWidth) ||
		InPropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationGeneratorConfig, BoardPatternHeight) ||
		InPropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationGeneratorConfig, BoardSquareSize))
	{
		Engine->Reset(State->Config.Get(), CaptureData.Get());
		ImageViewer->ResetState();
	}
}

void SMetaHumanCalibrationGeneratorWindow::RegisterImageViewerTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	const auto& ImageViewerTabSpawner = [this](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
		{
			const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
				.Label(LOCTEXT("ImageViewerTabLabel", "Image Viewer"))
				.CanEverClose(false)
				.OnCanCloseTab_Lambda([]()
									  {
										  return false;
									  });

			ImageViewer = SNew(SMetaHumanCalibrationImageViewer, SpawnTabArgs.GetOwnerWindow(), DockTab)
				.FootageCaptureData(CaptureData.Get())
				.State(State.ToWeakPtr())
				.FrameSelected(this, &SMetaHumanCalibrationGeneratorWindow::OnFrameSelectionChanged)
				.DetectForFrame(this, &SMetaHumanCalibrationGeneratorWindow::DetectForFrame);

			DockTab->SetContent(
				ImageViewer.ToSharedRef()
			);

			return DockTab;
		};

	FTabSpawnerEntry& TabSpawnerEntry = InTabManager->RegisterTabSpawner(UE::MetaHuman::Private::ImageViewerTabName, FOnSpawnTab::CreateLambda(ImageViewerTabSpawner))
		.SetDisplayName(LOCTEXT("ImageViewerTabSpawner", "Image Viewer"));

	InWorkspaceItem->AddItem(TabSpawnerEntry.AsShared());
}

void SMetaHumanCalibrationGeneratorWindow::RegisterOptionsTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	const auto& OptionsTabSpawner = [this](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.Label(LOCTEXT("OptionsTabLabel", "Options"))
			.CanEverClose(false)
			.OnCanCloseTab_Lambda([]()
			{
				return false;
			});

		OptionsTab = SNew(SMetaHumanCalibrationOptionsWidget)
			.Object(State->Options.Get());

		DockTab->SetContent(
			OptionsTab.ToSharedRef()
		);

		return DockTab;
	};

	FTabSpawnerEntry& TabSpawnerEntry = InTabManager->RegisterTabSpawner(UE::MetaHuman::Private::OptionsTabName, FOnSpawnTab::CreateLambda(OptionsTabSpawner))
		.SetDisplayName(LOCTEXT("OptionsTabSpawner", "Options"));

	InWorkspaceItem->AddItem(TabSpawnerEntry.AsShared());
}

void SMetaHumanCalibrationGeneratorWindow::RegisterConfigTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	const auto& ConfigTabSpawner = [this](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.Label(LOCTEXT("ConfigTabLabel", "Config"))
			.CanEverClose(false)
			.OnCanCloseTab_Lambda([]()
			{
				return false;
			});

		ConfigTab = SNew(SMetaHumanCalibrationConfigWidget)
			.Object(State->Config.Get())
			.OnObjectChanged(this, &SMetaHumanCalibrationGeneratorWindow::OnDetailsPropertyChanged);

		DockTab->SetContent(
			ConfigTab.ToSharedRef()
		);

		return DockTab;
	};

	FTabSpawnerEntry& TabSpawnerEntry = InTabManager->RegisterTabSpawner(UE::MetaHuman::Private::ConfigTabName, FOnSpawnTab::CreateLambda(ConfigTabSpawner))
		.SetDisplayName(LOCTEXT("ConfigTabSpawner", "Config"));

	InWorkspaceItem->AddItem(TabSpawnerEntry.AsShared());
}

FReply SMetaHumanCalibrationGeneratorWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsShiftDown())
	{
		double Step = FMath::IsNearlyZero(CaptureData->Metadata.FrameRate) ? 60.0 : CaptureData->Metadata.FrameRate;

		if (InKeyEvent.GetKey() == EKeys::A)
		{
			ImageViewer->PreviousFrame(Step);
		}
		else if (InKeyEvent.GetKey() == EKeys::D)
		{
			ImageViewer->NextFrame(Step);
		}
	}
	else
	{
		if (InKeyEvent.GetKey() == EKeys::A)
		{
			ImageViewer->PreviousFrame();
		}
		else if (InKeyEvent.GetKey() == EKeys::D)
		{
			ImageViewer->NextFrame();
		}
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMetaHumanCalibrationGeneratorWindow::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		ImageViewer->SelectCurrentFrame();
	}
	else if (InKeyEvent.GetKey() == EKeys::F)
	{
		ImageViewer->ResetView();
	}

	return SCompoundWidget::OnKeyUp(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
