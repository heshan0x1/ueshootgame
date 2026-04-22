// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "CaptureData.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenu.h"
#include "ToolMenuDelegates.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"

#include "Widgets/SCalibrationDiagnosticsWindow.h"
#include "UMetaHumanRobustFeatureMatcher.h"

#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"

#include "Style/MetaHumanCalibrationStyle.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationDiagnosticsModule"

class FMetaHumanCalibrationDiagnosticsModule : public IModuleInterface
{
private:

	static const FName TabName;

public:

	virtual void StartupModule() override
	{
		RegisterTabSpawner();

		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UFootageCaptureData::StaticClass());
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
		{
			if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
			{
				const TAttribute<FText> Label = LOCTEXT("CalibrationDiagnostics", "Calibration Diagnostics");
				const TAttribute<FText> ToolTip = LOCTEXT("CalibrationDiagnostics_Tooltip", "Calibration diagnostics for the stereo footage");
				const FSlateIcon Icon(FMetaHumanCalibrationStyle::Get().GetStyleSetName(), "MetaHumanCalibration.Diagnostics.Icon");
				
				FToolUIAction UIAction;
				UIAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this](const FToolMenuContext& InContext)
				{
					const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);

					UFootageCaptureData* FootageCaptureData = Context->LoadFirstSelectedObject<UFootageCaptureData>();
					if (FootageCaptureData)
					{
						TSharedPtr<SDockTab> DockTab = FGlobalTabmanager::Get()->TryInvokeTab(TabName);
						if (DockTab)
						{
							DockTab->SetContent(
								SNew(SCalibrationDiagnosticsWindow, DockTab->GetParentWindow(), DockTab.ToSharedRef())
								.FootageCaptureData(FootageCaptureData));
						}
					}
				});
				InSection.AddMenuEntry("DiagnosticsFootageCaptureDataCalibration", Label, ToolTip, Icon, UIAction);
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
					.Label(LOCTEXT("MainTabTitle", "Calibration Diagnostics"))
					.TabRole(ETabRole::MajorTab)
					.OnTabClosed_Raw(this, &FMetaHumanCalibrationDiagnosticsModule::OnClose);

				return DockTab;
			};

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateLambda(MoveTemp(SpawnMainTab)))
			.SetDisplayName(LOCTEXT("MainTabTitle", "Calibration Diagnostics"))
			.SetTooltipText(LOCTEXT("CalibrationDiagnosticsToolTip", "Provides calibration diagnostics on the performance footage"))
			.SetAutoGenerateMenuEntry(false)
			.SetMenuType(ETabSpawnerMenuType::Hidden);
	}

	void OnClose(TSharedRef<SDockTab> InClosingTab)
	{
		TSharedPtr<SCalibrationDiagnosticsWindow> Window =
			StaticCastSharedPtr<SCalibrationDiagnosticsWindow>(InClosingTab->GetContent().ToSharedPtr());

		if (Window)
		{
			Window->OnClose();
		}
	}
};

const FName FMetaHumanCalibrationDiagnosticsModule::TabName = TEXT("MetaHumanCalibrationDiagnostics");

IMPLEMENT_MODULE(FMetaHumanCalibrationDiagnosticsModule, MetaHumanCalibrationDiagnostics)

#undef LOCTEXT_NAMESPACE