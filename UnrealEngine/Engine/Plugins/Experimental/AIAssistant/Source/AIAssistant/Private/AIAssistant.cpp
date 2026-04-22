// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistant.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/SceneViewport.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#include "AIAssistantCommands.h"
#include "AIAssistantInputProcessor.h"
#include "AIAssistantSlateQuerier.h"
#include "AIAssistantStyle.h"
#include "AIAssistantWebBrowser.h"

#define LOCTEXT_NAMESPACE "FAIAssistantModule"


//
// Statics
//


static const FName AIAssistantTabName("AIAssistant");


//
// FAIAssistantModule
//


/*virtual*/ void FAIAssistantModule::StartupModule()
{
	GConfig->GetBool(TEXT("AIAssistant"), TEXT("bIsEnabled"), bIsAIAssistantEnabled, GEditorIni);

	if (!bIsAIAssistantEnabled)
	{
		return;
	}
	
		
	FAIAssistantStyle::Initialize();
	FAIAssistantStyle::ReloadTextures();


	FAIAssistantCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAIAssistantCommands::Get().OpenAIAssistantTab,
		FExecuteAction::CreateRaw(this, &FAIAssistantModule::OnOpenPluginTab),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FAIAssistantCommands::Get().SummonAIAssistantTab,
		FExecuteAction::CreateRaw(this, &FAIAssistantModule::OnTogglePluginTabBasedOnCursorLocation),
		FCanExecuteAction());

	PluginCommands->MapAction(
		FAIAssistantCommands::Get().AISlateQueryCommand,
		FExecuteAction::CreateRaw(this, &FAIAssistantModule::OnSlateQuery),
		FCanExecuteAction());


	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAIAssistantModule::RegisterMenus));


	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AIAssistantTabName, FOnSpawnTab::CreateRaw(this, &FAIAssistantModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FAIAssistantTabTitle", "AI Assistant"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon("AIAssistantStyle","AIAssistant.OpenPluginWindow"))
		// Sidebar tabs stil cause some issue with the panel drawer
		.SetCanSidebarTab(false);


	InputProcessor = MakeShared<FAIAssistantInputProcessor>(PluginCommands);

	if (GEditor)
	{
		RegisterStatusBarPanelDrawerSummon();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAIAssistantModule::RegisterStatusBarPanelDrawerSummon);
	}

	// Some scenarios, like commandlets, don't have slate initialized
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
	}
}


/*virtual*/ void FAIAssistantModule::ShutdownModule()
{
	if (!bIsAIAssistantEnabled)
	{
		return;
	}

	
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}

	UnregisterStatusBarPanelDrawerSummon();
	InputProcessor.Reset();


	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAIAssistantStyle::Shutdown();

	FAIAssistantCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIAssistantTabName);
}

void FAIAssistantModule::OnOpenPluginTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AIAssistantTabName);
}

void FAIAssistantModule::OnTogglePluginTabBasedOnCursorLocation()
{
	FWidgetPath WidgetPath = UE::AIAssistant::SlateQuerier::GetWidgetPathUnderCursor();

	TSharedPtr<FTabManager> TabManager;
	if (WidgetPath.IsValid())
	{
		TabManager = FGlobalTabmanager::Get()->GetSubTabManagerForWindow(WidgetPath.GetWindow());
	}


	if (TabManager)
	{
		// Leave the parent window to null so it will use the primary area
		TabManager->TryToggleTabInPanelDrawer(AIAssistantTabName, {});
	}
	else
	{
		OnOpenPluginTab();
	}
}

void FAIAssistantModule::OnOpenPluginTab(const FWidgetPath& InWidgetPath)
{
	TSharedPtr<FTabManager> TabManager;
	if (InWidgetPath.IsValid())
	{
		TabManager = FGlobalTabmanager::Get()->GetSubTabManagerForWindow(InWidgetPath.GetWindow());
	}


	if (TabManager)
	{
		// Leave the parent window to null so it will use the primary area
		TabManager->TryOpenTabInPanelDrawer(AIAssistantTabName, {});
	}
	else
	{
		OnOpenPluginTab();
	}
}

void FAIAssistantModule::OnSlateQuery()
{
	UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
	if (EditorEngine && EditorEngine->IsPlayingSessionInEditor())
	{
		// PIE is currently active
		if (GEngine->GameViewport)
		{
			FSceneViewport* SceneViewport = GEngine->GameViewport->GetGameViewport();
			if (SceneViewport)
			{
				if (SceneViewport->HasMouseCapture())
				{
					// If mouse is currently captured by PIE, do nothing.
					return;
				}
			}
		}
	}

	FWidgetPath WidgetPath = UE::AIAssistant::SlateQuerier::GetWidgetPathUnderCursor();
	if (WidgetPath.IsValid())
	{
		// ..idempotent, call before query to ensure tab is open and guarantee a valid AIAssistantWebBrowserWidget
		OnOpenPluginTab(WidgetPath);
		UE::AIAssistant::SlateQuerier::QueryAIAssistantAboutSlateWidget(WidgetPath);
	}
}


TSharedPtr<SAIAssistantWebBrowser> FAIAssistantModule::GetAIAssistantWebBrowserWidget()
{
	return AIAssistantWebBrowserWidget;
}


void FAIAssistantModule::ShowContextMenu(const FString& SelectedString, const FVector2f& ClientLocation) const
{
	if (!AIAssistantWebBrowserWidget.IsValid())
	{
		return;
	}

	const bool bIsSelectedTextValid = (!SelectedString.IsEmpty());


	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		FText::FromString("Copy"),
		FText::FromString("Copy selected text to the clipboard."),
		FSlateIcon("AIAssistantStyle", "AIAssistant.Copy"),
		FUIAction(
			FExecuteAction::CreateLambda([SelectedString]() -> void
				{
					// When chosen.

					FPlatformApplicationMisc::ClipboardCopy(*SelectedString);
				}),
			FCanExecuteAction::CreateLambda([bIsSelectedTextValid]() -> bool
				{
					// Whether is enabled.

					return bIsSelectedTextValid;
				})
		)
	);


	const FGeometry Geometry = AIAssistantWebBrowserWidget->GetCachedGeometry();
	const FVector2f ScreenLocation = Geometry.LocalToAbsolute(ClientLocation);

	FSlateApplication::Get().PushMenu(
		AIAssistantWebBrowserWidget.ToSharedRef(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		ScreenLocation,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
	);
}


void FAIAssistantModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner().
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Window");

		FToolMenuSection& Section = Menu->AddSection("Assistance", LOCTEXT("AIAssistantMenuSection_Label", "Assistance"), FToolMenuInsert("GetContent", EToolMenuInsertType::Before));

		Section.AddMenuEntryWithCommandList(FAIAssistantCommands::Get().OpenAIAssistantTab, PluginCommands, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon("AIAssistantStyle", "AIAssistant.OpenPluginWindow"));
	}

}

void FAIAssistantModule::RegisterStatusBarPanelDrawerSummon()
{
	if (GEditor)
	{
		// Load the module in case it wasn't yet loaded
		FModuleManager::Get().LoadModuleChecked(TEXT("StatusBar"));
		if (UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>())
		{
			StatusBarPanelDrawerSummonHandle = StatusBarSubsystem->RegisterPanelDrawerSummon(
				UStatusBarSubsystem::FRegisterPanelDrawerSummonDelegate::FDelegate::CreateRaw(this, &FAIAssistantModule::GenerateStatusBarPanelDrawerSummon)
			);
		}
	}
}

void FAIAssistantModule::UnregisterStatusBarPanelDrawerSummon()
{
	if (GEditor)
	{
		if (UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>())
		{
			StatusBarSubsystem->UnregisterPanelDrawerSummon(StatusBarPanelDrawerSummonHandle);
		}
	}
}

void FAIAssistantModule::GenerateStatusBarPanelDrawerSummon(TArray<UStatusBarSubsystem::FTabIdAndButtonLabel>& OutTabIdsAndLabels, const TSharedRef<SDockTab>& InParentTab) const
{
	OutTabIdsAndLabels.Emplace(AIAssistantTabName, LOCTEXT("StatusBarSummonAI", "Ask AI"));
}

TSharedRef<SDockTab> FAIAssistantModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SDockTab> DockTabWidget = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Lambda(
			[this](TSharedRef<SDockTab>) -> void
			{
				AIAssistantWebBrowserWidget.Reset();
			});

	// Work around for the web browser drawing out of bounds while doing the on demand clipping
	AIAssistantWebBrowserWidget = SNew(SAIAssistantWebBrowser, DockTabWidget)
		.Clipping(EWidgetClipping::ClipToBounds);

	DockTabWidget->SetContent(AIAssistantWebBrowserWidget.ToSharedRef());

	return DockTabWidget;
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAIAssistantModule, AIAssistant)
