// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerformanceCapture.h"

#include "EditorUtilityWidgetBlueprint.h"
#include "PerformanceCaptureStyle.h"
#include "PerformanceCaptureCommands.h"
#include "ISettingsModule.h"
#include "LevelEditorOutlinerSettings.h"
#include "MessageLogModule.h"
#include "PCapAssetDefinition.h"
#include "PCapDatabase.h"
#include "PCapSettings.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"

static const FName PerformanceCapturePanelTabName("PerformanceCaptureTab");

#define LOCTEXT_NAMESPACE "FPerformanceCaptureModule"

DEFINE_LOG_CATEGORY(LogPCap);

const FLazyName FPerformanceCaptureModule::MessageLogName = TEXT("PerformanceCapture");

void FPerformanceCaptureModule::StartupModule()
{
	if(ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "PerformanceCapture", LOCTEXT("RuntimeSettingsName", "Performance Capture"), LOCTEXT("RuntimeSettingsDescription", "Performance Capture"), GetMutableDefault<UPerformanceCaptureSettings>());
	}

	FPerformanceCaptureStyle::Initialize();
	FPerformanceCaptureStyle::ReloadTextures();

	FPerformanceCaptureCommands::Register();
	
	PluginCommands = MakeShared<FUICommandList>();

	PluginCommands->MapAction(
		FPerformanceCaptureCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FPerformanceCaptureModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPerformanceCaptureModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PerformanceCapturePanelTabName, FOnSpawnTab::CreateRaw(this, &FPerformanceCaptureModule::OnSpawnMocapManager))
		.SetDisplayName(NSLOCTEXT("PerformanceCapture", "MocapManagerTabTitle", "Mocap Manager"))
		.SetTooltipText(NSLOCTEXT("PerformanceCapture", "PerformanceCaptureTooltipText", "Open the Mocap Manager tab"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FPerformanceCaptureStyle::GetStyleSetName(), "PerformanceCapture.MocapManagerTabIcon", "PerformanceCapture.MocapManagerTabIcon.Small")
		);

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.RegisterLogListing(MessageLogName, LOCTEXT("MessageLogPerformanceCaptureName", "PerformanceCapture"));

}

void FPerformanceCaptureModule::ShutdownModule()
{
	//Clean up settings
	if(ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "PerformanceCapture");
	}

	//Clean up nomad tab spawner
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PerformanceCapturePanelTabName);
	

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FPerformanceCaptureStyle::Shutdown();

	FPerformanceCaptureCommands::Unregister();
	
}

TSharedRef<SDockTab> FPerformanceCaptureModule::OnSpawnMocapManager(const FSpawnTabArgs& SpawnTabArgs)
{
	const UPerformanceCaptureSettings* Settings = GetDefault<UPerformanceCaptureSettings>();
	
	if(Settings->MocapManagerUI.IsValid())
	{
		UEditorUtilityWidgetBlueprint* MocapManagerEW = LoadObject<UEditorUtilityWidgetBlueprint>(NULL, *Settings->MocapManagerUI.ToString(), NULL, LOAD_None, NULL);
	
		TSharedRef<SDockTab> TabWidget = MocapManagerEW->SpawnEditorUITab(SpawnTabArgs);
		
			return TabWidget;
	}
	
	{
	//Define message
	FText WidgetText = FText::Format(
	LOCTEXT("WindowWidgetText", "Performance Capture Project settings missing a valid UI Widget"),
	FText::FromString(TEXT("FPerformanceCaptureModule::OnSpawnMocapManager")),
	FText::FromString(TEXT("PerformanceCapture.cpp"))
	);
		//Create default tab message
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				// Put your tab content here!
		
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(WidgetText)
				]
			];
	}
}

void FPerformanceCaptureModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PerformanceCapturePanelTabName);
}

void FPerformanceCaptureModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window.VirtualProduction");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("VirtualProduction");
			Section.AddMenuEntryWithCommandList(FPerformanceCaptureCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FPerformanceCaptureCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPerformanceCaptureModule, PerformanceCaptureWorkflow)

