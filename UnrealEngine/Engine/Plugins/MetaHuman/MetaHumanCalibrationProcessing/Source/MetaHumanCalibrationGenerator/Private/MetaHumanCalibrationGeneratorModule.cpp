// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "CaptureData.h"
#include "ImgMediaSource.h" 
#include "Widgets/SMetaHumanCalibrationGeneratorWindow.h"
#include "Settings/MetaHumanCalibrationGeneratorSettings.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenu.h"
#include "ToolMenuDelegates.h"
#include "Misc/MessageDialog.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"

#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationGeneratorModule"

class FMetaHumanCalibrationGeneratorModule : public IModuleInterface
{
private:

	static const FName TabName;

public:

	virtual void StartupModule() override
	{
		RegisterSettings();

		RegisterTabSpawner();

		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UFootageCaptureData::StaticClass());
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
		{
			if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
			{
				const TAttribute<FText> Label = LOCTEXT("GenerateCalibration", "Generate Calibration");
				const TAttribute<FText> ToolTip = LOCTEXT("GenerateCalibration_Tooltip", "Generate calibration lens files for the stereo camera pair");
				const FSlateIcon Icon(FMetaHumanCalibrationStyle::Get().GetStyleSetName(), "MetaHumanCalibration.Generator.Icon");
				
				FToolUIAction UIAction;
				UIAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this](const FToolMenuContext& InContext)
				{
					const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);
					if (Context)
					{
						UFootageCaptureData* FootageCaptureData = Context->LoadFirstSelectedObject<UFootageCaptureData>();
						if (IsValid(FootageCaptureData))
						{
							if (FootageCaptureData->ImageSequences.Num() < 2)
							{
								FText Text = LOCTEXT("GenerateCalibration_InvalidInput", "Could not run Generate Calibration. At least 2 Image Sequences required in the Footage Capture Data asset.");
								FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, Text);
							}
							else if (!IsValid(FootageCaptureData->ImageSequences[0]) || !IsValid(FootageCaptureData->ImageSequences[1]))
							{
								FText Text = LOCTEXT("GenerateCalibration_NullImageSequences", "Could not run Generate Calibration. Image Sequences in the Footage Capture Data cannot be null.");
								FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, Text);
							}
							else
							{
								TSharedPtr<SDockTab> DockTab = FGlobalTabmanager::Get()->TryInvokeTab(TabName);
								if (DockTab)
								{
									DockTab->SetContent(
										SNew(SMetaHumanCalibrationGeneratorWindow, DockTab->GetParentWindow(), DockTab.ToSharedRef())
										.CaptureData(FootageCaptureData));
								}
							}
						}
					}
				});
				InSection.AddMenuEntry("GenerateFootageCaptureDataCalibration", Label, ToolTip, Icon, UIAction);
			}
		}));
	}

	virtual void ShutdownModule() override
	{
		FGlobalTabmanager::Get()->UnregisterTabSpawner(TabName);
	}

private:

	void RegisterTabSpawner()
	{
		auto SpawnMainTab = [this](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
			{
				const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
					.Label(LOCTEXT("MainTabTitle", "Calibration Generator"))
					.TabRole(ETabRole::MajorTab)
					.OnTabClosed_Raw(this, &FMetaHumanCalibrationGeneratorModule::OnClose);

				return DockTab;
			};

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateLambda(MoveTemp(SpawnMainTab)))
			.SetDisplayName(LOCTEXT("MainTabTitle", "Calibration Generator"))
			.SetTooltipText(LOCTEXT("CalibrationGeneratorToolTip", "Generate calibration from the calibration footage"))
			.SetAutoGenerateMenuEntry(false)
			.SetMenuType(ETabSpawnerMenuType::Hidden);
	}

	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "MetaHuman Calibration Generator",
											 LOCTEXT("MHCalibrationGeneratorSettingsName", "MetaHuman Calibration Generator"),
											 LOCTEXT("MHCalibrationGeneratorSettingsDescription", "Settings for MetaHuman Calibration Generator plugin"),
											 GetMutableDefault<UMetaHumanCalibrationGeneratorSettings>());

		}
	}

	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "MetaHuman Calibration Generator");
		}
	}

	void OnClose(TSharedRef<SDockTab> InClosingTab)
	{
		TSharedPtr<SMetaHumanCalibrationGeneratorWindow> Window =
			StaticCastSharedPtr<SMetaHumanCalibrationGeneratorWindow>(InClosingTab->GetContent().ToSharedPtr());

		if (Window)
		{
			Window->OnClose();
		}
	}
};

const FName FMetaHumanCalibrationGeneratorModule::TabName = TEXT("MetaHumanCalibration");

IMPLEMENT_MODULE(FMetaHumanCalibrationGeneratorModule, MetaHumanCalibrationGenerator)

#undef LOCTEXT_NAMESPACE