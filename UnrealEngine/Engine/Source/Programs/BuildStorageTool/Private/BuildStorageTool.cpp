// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildStorageTool.h"
#include "Experimental/ZenServerInterface.h"
#include "Features/IModularFeatures.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CompilationResult.h"
#include "Misc/MessageDialog.h"
#include "Misc/Optional.h"
#include "Misc/Timespan.h"
#include "Modules/BuildVersion.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RequiredProgramMainCPPInclude.h"
#include "SBuildActivity.h"
#include "SBuildSelection.h"
#include "SBuildLogin.h"
#include "SMessageDialog.h"
#include "StandaloneRenderer.h"
#include "BuildStorageToolStyle.h"
#include "BuildStorageToolHelpWidget.h"
#include "Tasks/Task.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "ZenServiceInstanceManager.h"
#include "BuildServiceInstanceManager.h"
#include "Version/AppVersionDefines.h"
#include "Parameters/BuildStorageToolParametersBuilder.h"
#include "OutputLogCreationParams.h"
#include "OutputLogModule.h"
#include "OutputLogSettings.h"

#if PLATFORM_WINDOWS
#include "Runtime/Launch/Resources/Windows/Resource.h"
#include "Windows/WindowsApplication.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shellapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX
#include "UnixCommonStartup.h"
#elif PLATFORM_MAC
#include "Mac/MacProgramDelegate.h"
#include "LaunchEngineLoop.h"
#else
#error "Unsupported platform!"
#endif

#define LOCTEXT_NAMESPACE "BuildStorageTool"

// These macros are not properly defined by UBT in the case of an engine program with bTreatAsEngineModule=true
// So define them here as a workaround
#define IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()
#define IMPLEMENT_SIGNING_KEY_REGISTRATION()
IMPLEMENT_APPLICATION(BuildStorageTool, "BuildStorageTool");

DEFINE_LOG_CATEGORY(LogBuildStorageTool);

static void OnRequestExit()
{
	RequestEngineExit(TEXT("BuildStorageTool Closed"));
}

static void HideOnCloseOverride(const TSharedRef<SWindow>& WindowBeingClosed)
{
	WindowBeingClosed->HideWindow();
}

class FBuildStorageToolApp
{
	FCriticalSection CriticalSection;
	FSlateApplication& Slate;
	TSharedPtr<SWindow> Window;
	TSharedPtr<SNotificationItem> CompileNotification;
	TSharedPtr<UE::Zen::FServiceInstanceManager> ZenServiceInstanceManager;
	TSharedPtr<UE::Zen::Build::FServiceInstanceManager> BuildServiceInstanceManager;
	const FBuildStorageToolParameters& ToolParameters;

	std::atomic<bool> bLatentExclusiveOperationActive = false;

	void ExitTool()
	{
		FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());
	}

	void OnAboutCommandPressed()
	{
		FSlateApplication& SlateApplicaton = FSlateApplication::Get();

		if (SlateApplicaton.GetActiveModalWindow() != nullptr)
		{
			return;
		}

		TSharedPtr<SWidget> ParentWidget = SlateApplicaton.GetUserFocusedWidget(0);
		if (!ensure(ParentWidget))
		{
			return;
		}

		// Determine the position of the window so that it will spawn near the mouse, but not go off the screen.
		const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
		FSlateRect Anchor(CursorPos.X, CursorPos.Y, CursorPos.X, CursorPos.Y);
		FVector2D AdjustedSummonLocation = FSlateApplication::Get().CalculatePopupWindowPosition(Anchor, FVector2D(441, 537), true, FVector2D::ZeroVector, Orient_Horizontal);

		TSharedPtr<SWindow> WindowDialog = SNew(SWindow)
			.AutoCenter(EAutoCenter::None)
			.ScreenPosition(AdjustedSummonLocation)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.SizingRule(ESizingRule::Autosized)
			.Title(LOCTEXT("HelpAboutHeader", "About"));

		WindowDialog->SetContent(SNew(SBuildStorageToolHelpWidget)
			.ToolParameters(&ToolParameters)
		);

		FSlateApplication::Get().AddModalWindow(WindowDialog.ToSharedRef(), ParentWidget);
	}

	bool CanExecuteExclusiveAction()
	{
		return !bLatentExclusiveOperationActive;
	}

	TSharedRef< SWidget > MakeMainMenu()
	{
		FMenuBarBuilder MenuBuilder( nullptr );
		{
			MenuBuilder.AddPullDownMenu(
				LOCTEXT( "FileMenu", "File" ),
				LOCTEXT( "FileMenu_ToolTip", "Opens the file menu" ),
				FOnGetContent::CreateRaw( this, &FBuildStorageToolApp::FillFileMenu ) );

			MenuBuilder.AddPullDownMenu(
				LOCTEXT("ToolsMenu", "Tools"),
				LOCTEXT("ToolsMenu_ToolTip", "Opens the tools menu"),
				FOnGetContent::CreateRaw(this, &FBuildStorageToolApp::FillToolsMenu) );

			MenuBuilder.AddPullDownMenu(
				LOCTEXT( "HelpMenu", "Help" ),
				LOCTEXT( "HelpMenu_ToolTip", "Opens the help menu" ),
				FOnGetContent::CreateRaw( this, &FBuildStorageToolApp::FillHelpMenu ) );
		}

		// Create the menu bar
		TSharedRef< SWidget > MenuBarWidget = MenuBuilder.MakeWidget();
		MenuBarWidget->SetVisibility( EVisibility::Visible ); // Work around for menu bar not showing on Mac

		return MenuBarWidget;
	}

	void OpenLogWindow()
	{
		/*** Output Log Widget ***/
		FOutputLogModule& OutputLogModule = FModuleManager::Get().LoadModuleChecked<FOutputLogModule>("OutputLog");

		// hide the debug console
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("OutputLogModule.HideConsole"));
		if(ensure(CVar))
		{
			CVar->Set(true);
		}

		// setup OutputLog settings
		UOutputLogSettings* Settings = GetMutableDefault<UOutputLogSettings>();
		if(Settings)
		{
			Settings->CategoryColorizationMode = ELogCategoryColorizationMode::ColorizeWholeLine;
		}

		// Open new slate window
		FSlateApplication::Get().AddWindowAsNativeChild(
			SNew(SWindow)
			.Title(LOCTEXT("LogWindowTitle", "Build Storage Tool Log"))
			.ClientSize(FVector2D(1200.0f, 600.0f))
			.ActivationPolicy(EWindowActivationPolicy::Always)
			.SizingRule(ESizingRule::UserSized)
			.IsTopmostWindow(false)
			.FocusWhenFirstShown(false)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.HasCloseButton(true)
			[
				OutputLogModule.MakeOutputLogWidget(FOutputLogCreationParams())
			],
			Window.ToSharedRef()
		);
	}

	TSharedRef<SWidget> FillToolsMenu()
	{
		const bool bCloseSelfOnly = false;
		const bool bSearchable = false;
		const bool bRecursivelySearchable = false;

		FMenuBuilder MenuBuilder(true,
			nullptr,
			TSharedPtr<FExtender>(),
			bCloseSelfOnly,
			&FCoreStyle::Get(),
			bSearchable,
			NAME_None,
			bRecursivelySearchable);

		MenuBuilder.AddMenuEntry(
					LOCTEXT("OpenLogWindow", "Open Log Window"),
					LOCTEXT("OpenLogWindow_ToolTip", "Opens the Log Window"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateRaw( this, &FBuildStorageToolApp::OpenLogWindow )
					),
					NAME_None,
					EUserInterfaceActionType::Button
				);
		
		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FillFileMenu()
	{
		const bool bCloseSelfOnly = false;
		const bool bSearchable = false;
		const bool bRecursivelySearchable = false;

		FMenuBuilder MenuBuilder(true,
			nullptr,
			TSharedPtr<FExtender>(),
			bCloseSelfOnly,
			&FCoreStyle::Get(),
			bSearchable,
			NAME_None,
			bRecursivelySearchable);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Exit", "Exit"),
			LOCTEXT("Exit_ToolTip", "Exits the Storage Server UI"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw( this, &FBuildStorageToolApp::ExitTool ),
				FCanExecuteAction::CreateRaw(this, &FBuildStorageToolApp::CanExecuteExclusiveAction)
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FillHelpMenu()
	{
		const bool bCloseSelfOnly = false;
		const bool bSearchable = false;
		const bool bRecursivelySearchable = false;

		FMenuBuilder MenuBuilder(true,
			nullptr,
			TSharedPtr<FExtender>(),
			bCloseSelfOnly,
			&FCoreStyle::Get(),
			bSearchable,
			NAME_None,
			bRecursivelySearchable);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("About", "About"),
			LOCTEXT("About_ToolTip", "Show the about dialog"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw( this, &FBuildStorageToolApp::OnAboutCommandPressed ),
				FCanExecuteAction::CreateRaw(this, &FBuildStorageToolApp::CanExecuteExclusiveAction)
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		return MenuBuilder.MakeWidget();
	}

public:
	FBuildStorageToolApp(FSlateApplication& InSlate, const FBuildStorageToolParameters& InToolParameters)
		: Slate(InSlate), ToolParameters(InToolParameters)
	{
		ZenServiceInstanceManager = MakeShared<UE::Zen::FServiceInstanceManager>();
		BuildServiceInstanceManager = MakeShared<UE::Zen::Build::FServiceInstanceManager>();
		InstallMessageDialogOverride();
	}

	virtual ~FBuildStorageToolApp()
	{
		RemoveMessageDialogOverride();
	}

	void Run()
	{
		Window =
			SNew(SWindow)
			.Title(GetWindowTitle())
			.ClientSize(FVector2D(1000.0f, 600.0f))
			.ActivationPolicy(EWindowActivationPolicy::Always)
			.SizingRule(ESizingRule::UserSized)
			.IsTopmostWindow(false)
			.FocusWhenFirstShown(false)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			.HasCloseButton(true)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)

					// Menu
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						MakeMainMenu()
					]

					// Login panel
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 5.0f, 0.0f)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Top)
					[
						SNew(SBuildLogin)
						.ZenServiceInstance(ZenServiceInstanceManager.ToSharedRef(), &UE::Zen::FServiceInstanceManager::GetZenServiceInstance)
						.BuildServiceInstance(BuildServiceInstanceManager.ToSharedRef(), &UE::Zen::Build::FServiceInstanceManager::GetBuildServiceInstance)
					]

					// Selection panel
					+ SVerticalBox::Slot()
					.Padding(0.0f, 10.0f, 5.0f, 10.0f)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SBuildSelection)
						.ZenServiceInstance(ZenServiceInstanceManager.ToSharedRef(), &UE::Zen::FServiceInstanceManager::GetZenServiceInstance)
						.BuildServiceInstance(BuildServiceInstanceManager.ToSharedRef(), &UE::Zen::Build::FServiceInstanceManager::GetBuildServiceInstance)
						.OnBuildTransferStarted_Lambda([this]
							(UE::Zen::Build::FBuildServiceInstance::FBuildTransfer Transfer, FStringView Name, FStringView Platform)
							{
								BuildActivity->AddBuildTransfer(Transfer, Name, Platform);
							})
					]

					// Activity panel
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 5.0f, 0.0f)
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Bottom)
					[
						SAssignNew(BuildActivity, SBuildActivity)
						.ZenServiceInstance(ZenServiceInstanceManager.ToSharedRef(), &UE::Zen::FServiceInstanceManager::GetZenServiceInstance)
						.BuildServiceInstance(BuildServiceInstanceManager.ToSharedRef(), &UE::Zen::Build::FServiceInstanceManager::GetBuildServiceInstance)
					]
				]
			];

		Slate.AddWindow(Window.ToSharedRef(), true);

		// Setting focus seems to have to happen after the Window has been added
		Slate.ClearKeyboardFocus(EFocusCause::Cleared);

		// loop until the app is ready to quit
		while (!IsEngineExitRequested())
		{
			BeginExitIfRequested();

			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FTSTicker::GetCoreTicker().Tick(FApp::GetDeltaTime());
			Slate.PumpMessages();
			Slate.Tick();

			FPlatformProcess::Sleep(1.0f / 30.0f);
		}

		// Make sure the window is hidden, because it might take a while for the background thread to finish.
		Window->HideWindow();
	}

private:
	FText GetWindowTitle()
	{
		FText ToolName = LOCTEXT("WindowTitle", "Unreal Build Storage Tool");
#if defined(BUILD_STORAGE_TOOL_CHANGELIST_STRING)
		return FText::Format(LOCTEXT("WindowTitle_VersionFormat", "{0} ({1})"), ToolName, FText::FromString(BUILD_STORAGE_TOOL_CHANGELIST_STRING));
#else
		return ToolName;
#endif
	}

	EAppReturnType::Type OnModalMessageDialog(EAppMsgCategory InMessageCategory, EAppMsgType::Type InMessage, const FText& InText, const FText& InTitle)
	{
		if (IsInGameThread() && FSlateApplication::IsInitialized() && FSlateApplication::Get().CanAddModalWindow())
		{
			return OpenModalMessageDialog_Internal(InMessageCategory, InMessage, InText, InTitle, Window);
		}
		else
		{
			return FPlatformMisc::MessageBoxExt(InMessage, *InText.ToString(), *InTitle.ToString());
		}
	}

	void InstallMessageDialogOverride()
	{
		FCoreDelegates::ModalMessageDialog.BindRaw(this, &FBuildStorageToolApp::OnModalMessageDialog);
	}

	void RemoveMessageDialogOverride()
	{
		FCoreDelegates::ModalMessageDialog.Unbind();
	}

	TSharedPtr<SBuildActivity> BuildActivity;
};

int BuildStorageToolMain(const TCHAR* CmdLine)
{
	ON_SCOPE_EXIT
	{
		RequestEngineExit(TEXT("Exiting"));
		FEngineLoop::AppPreExit();
		FModuleManager::Get().UnloadModulesAtShutdown();
		FEngineLoop::AppExit();
	};

	const FTaskTagScope Scope(ETaskTag::EGameThread);

	// start up the main loop
	GEngineLoop.PreInit(CmdLine);

	// ensure that the backlog is enabled
	if(GLog)
	{
		GLog->EnableBacklog(true);
	}

	FSystemWideCriticalSection SystemWideBuildStorageToolCritSec(TEXT("BuildStorageTool"));
	if (!SystemWideBuildStorageToolCritSec.IsValid())
	{
		return true;
	}
	check(GConfig && GConfig->IsReadyForUse());

	// Initialize high DPI mode
	FSlateApplication::InitHighDPI(true);

	FBuildStorageToolParametersBuilder ParametersBuilder;
	FBuildStorageToolParameters Parameters = ParametersBuilder.Build();
	
	{
		// Create the platform slate application (what FSlateApplication::Get() returns)
		TSharedRef<FSlateApplication> Slate = FSlateApplication::Create(MakeShareable(FPlatformApplicationMisc::CreateApplication()));

		{
			// Initialize renderer
			TSharedRef<FSlateRenderer> SlateRenderer = GetStandardStandaloneRenderer();

			// Try to initialize the renderer. It's possible that we launched when the driver crashed so try a few times before giving up.
			bool bRendererInitialized = Slate->InitializeRenderer(SlateRenderer, true);
			if (!bRendererInitialized)
			{
				// Close down the Slate application
				FSlateApplication::Shutdown();
				return false;
			}

			// Set the normal UE IsEngineExitRequested() when outer frame is closed
			Slate->SetExitRequestedHandler(FSimpleDelegate::CreateStatic(&OnRequestExit));

			// Prepare the custom Slate styles
			FBuildStorageToolStyle::Initialize();

			// Set the icon
			FAppStyle::SetAppStyleSet(FBuildStorageToolStyle::Get());

			// Run the inner application loop
			FBuildStorageToolApp App(Slate.Get(), Parameters);
			App.Run();

			// Clean up the custom styles
			FBuildStorageToolStyle::Shutdown();
		}

		// Close down the Slate application
		FSlateApplication::Shutdown();
	}

	return true;
}

#if PLATFORM_WINDOWS
int WINAPI WinMain(_In_ HINSTANCE hCurrInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	hInstance = hCurrInstance;
	return BuildStorageToolMain(GetCommandLineW())? 0 : 1;
}

#elif PLATFORM_LINUX
int UnixMainWrapper(const TCHAR* CommandLine)
{
	return BuildStorageToolMain(CommandLine) ? 0 : 1;
};
int main(int argc, char* argv[])
{
	return CommonUnixMain(argc, argv, &UnixMainWrapper);
}
#elif PLATFORM_MAC
int32 MacMainWrapper(const TCHAR* CommandLine)
{
	return BuildStorageToolMain(CommandLine) ? 0 : 1;
};
int main(int argc, char* argv[])
{
	[MacProgramDelegate mainWithArgc : argc argv : argv programMain : MacMainWrapper programExit : FEngineLoop::AppExit] ;
}
#else
#error "Unsupported platform!"
#endif

#undef LOCTEXT_NAMESPACE
