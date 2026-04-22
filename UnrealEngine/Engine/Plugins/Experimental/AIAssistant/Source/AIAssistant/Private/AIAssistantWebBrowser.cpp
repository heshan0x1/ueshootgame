// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantWebBrowser.h"

#include "Async/UniqueLock.h"
#include "Editor.h"
#include "WebBrowserModule.h"
#include "IWebBrowserWindow.h"
#include "Misc/AssertionMacros.h"
#include "Framework/Application/SlateApplication.h"
#include "Async/TaskGraphInterfaces.h"
#include "Widgets/Docking/SDockTab.h"

#include "AIAssistantConfig.h"
#include "AIAssistantLog.h"

using namespace UE::AIAssistant;


#define LOCTEXT_NAMESPACE "SAIAssistantWebBrowser"


// SWebBrowser::InitialURL() does not seem to load our initial URL. Loading after the SWebBrowser 
// is created works instead. TODO - Investigate.
#define UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG 0


void SAIAssistantWebBrowser::Construct(const FArguments& InArgs, const TSharedRef<SDockTab>& InParentTab)
{
	Config = FAIAssistantConfig::Load();
	
	// Web connection widget.	
	SAssignNew(WebConnectionWidget, SAIAssistantWebConnectionWidget)
		.WhenDisconnectedMessage(
			LOCTEXT("SAIAssistantWebBrowser_NotConnected", "AI Assistant is Not Connected"))
		.OnRequestConnectionState([this]() -> SAIAssistantWebConnectionWidget::EConnectionState
			{
				return GetConnectionState();
			})
		.OnReconnect_Lambda([this]() -> void
			{
				if (WebBrowserWidget.IsValid())
				{
					// Must reload to (1) clear any error page, (2) make sure the correct page is
					// actually loaded.
					InitializeWebApplication();
					LoadUrl(Config.MainUrl, EOpenBrowserMode::Embedded);
				}
			});


	// We need to do this mainly to initialize CEF (Chromium Embedded Framework.)
	// NOTE - We have not enabled the WebBrowserWidget for this plugin. If we had, then this would
	// not be necessary, and this would have been taken care of for us.
	TSharedPtr<IWebBrowserWindow> WebBrowserWindow;
	if (IWebBrowserSingleton* WebBrowserSingleton = IWebBrowserModule::Get().GetSingleton();
		WebBrowserSingleton)
	{
		FCreateBrowserWindowSettings WindowSettings;
		WindowSettings.BrowserFrameRate = 60;

		WebBrowserWindow = WebBrowserSingleton->CreateBrowserWindow(WindowSettings);
		WebBrowserWindow->SetParentDockTab(InParentTab);
	}
	else
	{
		UE_LOG(
			LogAIAssistant, Error, 
			TEXT(
				"Could not access Web Browser Singleton from Web Browser Module, so could not create Web "
				"Browser Window."));
		return;
	}

	
	// Web Browser.
	SAssignNew(WebBrowserWidget, SWebBrowser, /*passed to SWebBrowser ctr..*/ WebBrowserWindow)
#if UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG
		.InitialURL(Config.MainUrl) 
#endif
		.ShowControls(false)
		.ShowErrorMessage(false) // ..suppress showing error pages
		.Visibility(EVisibility::Visible)
		.OnBeforeNavigation_Lambda([this](const FString& Url, const FWebNavigationRequest& Request) -> bool
		{
			// 'Navigation' means an attempt to redirect the current browser window.
			bool bBlockNavigation = false;
			EnsureWebApplication();
			WithWebApplication(
				[this, &Url, &Request](
					TSharedRef<FWebApplication> WebApplication) -> void
				{
					WebApplication->OnBeforeNavigation(Url, Request);
				});
			return bBlockNavigation;
		})
		.OnBeforePopup_Lambda([this](/*no const&*/FString Url, /*no const&*/FString Frame) -> bool
		{
			// 'Popup' means an attempt to open a new browser window.
			LoadUrl(Url, EOpenBrowserMode::System);
			return true; // ..means block navigation in THIS browser
		})
		.OnConsoleMessage_Lambda([](
			const FString& Message, const FString& Source, int32 Line,
			EWebBrowserConsoleLogSeverity WebBrowserConsoleLogSeverity) -> void
		{
			// Receives messages from JavaScript.
			switch (WebBrowserConsoleLogSeverity)
			{
			case EWebBrowserConsoleLogSeverity::Error:
				// fall through.
			case EWebBrowserConsoleLogSeverity::Fatal:
				UE_LOG(LogAIAssistant, Display, TEXT("JavaScript Error: '%s' @ %s:%d"),
					*Message, *Source, Line);
				break;
			case EWebBrowserConsoleLogSeverity::Warning:
				UE_LOG(LogAIAssistant, Display, TEXT("JavaScript Warning: '%s' @ %s:%d"),
					*Message, *Source, Line);
				break;
			default:
				UE_LOG(LogAIAssistant, Display, TEXT("JavaScript: '%s' @ %s:%d"),
					*Message, *Source, Line);
				break;
			}
		})
		.OnLoadError_Lambda([this]() -> void
		{
			if (WebConnectionWidget.IsValid())
			{
				WebConnectionWidget->Disconnect();
			}
			EnsureWebApplication();
			WithWebApplication(
				[this](TSharedRef<FWebApplication> WebApplication) -> void
				{
					WebApplication->OnPageLoadError();
				});
		})
		.OnLoadCompleted_Lambda([this]() -> void
		{
			WithWebApplication(
				[this](TSharedRef<FWebApplication> WebApplication) -> void
				{
					WebApplication->OnPageLoadComplete();
				});

			// Set keyboard focus. This makes sure that a blinking cursor will appear in text
			// boxes that are clicked on. Without this, this isn't always the case.
			// NOTE - Make sure we run this in Game Thread, so FSlateApplication::Get() doesn't
			// assert. On Mac, the lambda we're in now will often get called from an AppKit thread.
			{
				TWeakPtr<SWebBrowser> WebBrowserWidgetWeak = WebBrowserWidget;
				AsyncTask(ENamedThreads::GameThread, [WebBrowserWidgetWeak]() -> void
					{
						// Safety checks, since we can rarely end up here during Editor shutdown.
						if (const TSharedPtr<SWebBrowser> WebBrowserWidget = WebBrowserWidgetWeak.Pin();
							WebBrowserWidget.IsValid() && FSlateApplication::IsInitialized())
						{
							FSlateApplication::Get().SetKeyboardFocus(
								WebBrowserWidget, EFocusCause::SetDirectly);
						}
					});
			}
		});
	
	// Widget switcher widget.
	TSharedRef<SWidget> WebBrowserSharedRef = WebBrowserWidget.ToSharedRef();
	TSharedRef<SWidget> WebConnectionSharedRef = WebConnectionWidget.ToSharedRef();
	SAssignNew(WebBrowserOrWebConnectionSwitcherWidget, SWidgetSwitcher)
		.WidgetIndex_Lambda([this, WebBrowserSharedRef, WebConnectionSharedRef]() -> int32
			{
				auto SelectedWidget = 
					WebConnectionWidget->GetConnectionState() ==
						SAIAssistantWebConnectionWidget::EConnectionState::Connected
					? WebBrowserSharedRef : WebConnectionSharedRef;
				return WebBrowserOrWebConnectionSwitcherWidget->GetWidgetIndex(SelectedWidget);
			})
		+SWidgetSwitcher::Slot()
			[
				WebBrowserSharedRef
			]
		+SWidgetSwitcher::Slot()
			[
				WebConnectionSharedRef
			];
	
	// Widget tree.
	ChildSlot
		[
			WebBrowserOrWebConnectionSwitcherWidget.ToSharedRef()
		];


	// Make the IME (Input Method Editor) work with HTML text input boxes on web pages,
	// for non-ASCII languages, like "Chinese (Traditional, Taiwan)".
	if (ITextInputMethodSystem* InputMethodSystem = FSlateApplication::Get().GetTextInputMethodSystem())
    {
		if (WebBrowserWidget.IsValid())
		{
			WebBrowserWidget->BindInputMethodSystem(InputMethodSystem);

			// If UE is being shut down, we need to unbind the Input Method System before our
			// destructor tries to do it. (Otherwise, during that time, Slate won't be in the
			// correct state, and we can crash on shutdown.) 
			TWeakPtr<SWebBrowser> WebBrowserWidgetWeak = WebBrowserWidget; 
			InputMethodSystemSlatePreShutdownDelegateHandle = FSlateApplication::Get().OnPreShutdown().AddLambda([WebBrowserWidgetWeak]() -> void
			{
				// NOTE - Slate must be initialized or we can crash.
				if (const TSharedPtr<SWebBrowser> WebBrowserWidgetLocal = WebBrowserWidgetWeak.Pin();
					FSlateApplication::IsInitialized() && WebBrowserWidgetLocal.IsValid())
				{
					WebBrowserWidgetLocal->UnbindInputMethodSystem();
				}
			});
		}
    }
    else
    {
		static const FText ErrorText = LOCTEXT(
			"SAIAssistantWebBrowser_NonAsciiLanguageNotSupported",
			"AI Assistant could not get an Input Method System. You will not be able to input "
			"non-ASCII languages with AI Assistant.");
        UE_LOG(LogAIAssistant, Error, TEXT("%hs() - %s"), __func__, *ErrorText.ToString());
    }

	
#if !UE_AIA_SET_INITIAL_URL_VIA_WEB_BROWSER_SLATE_ARG
	InitializeWebApplication();
	LoadUrl(Config.MainUrl, EOpenBrowserMode::Embedded);
#endif
}

/*virtual*/ SAIAssistantWebBrowser::~SAIAssistantWebBrowser()
{
	InitializeWebApplication();
				
	// Unbind the Input Method Subsystem.
	// NOTE - Slate must be initialized or we can crash.
	if (FSlateApplication::IsInitialized() && WebBrowserWidget.IsValid())
	{
		WebBrowserWidget->UnbindInputMethodSystem();
	}
	
	// Remove the Input Method Subsystem pre-shutdown lambda.
	// NOTE - Slate must be initialized or we can crash.
	if (FSlateApplication::IsInitialized() && InputMethodSystemSlatePreShutdownDelegateHandle.IsValid())
	{
		FSlateApplication::Get().OnPreShutdown().Remove(InputMethodSystemSlatePreShutdownDelegateHandle);
	}

	// In case the web connection widget was trying to reconnect, stop it from reconnecting.
	if (WebConnectionWidget.IsValid())
	{
		WebConnectionWidget->StopReconnecting(); 
	}
}

void SAIAssistantWebBrowser::InitializeWebApplication()
{
	UE::TUniqueLock WebApplicationLock(WebApplicationMutex);
	MaybeWebApplication = MakeShared<FWebApplication>(
		FWebApplication::CreateWebApiFactory(*this, *this));
}

void SAIAssistantWebBrowser::EnsureWebApplication()
{
	UE::TUniqueLock WebApplicationLock(WebApplicationMutex);
	if (!MaybeWebApplication) InitializeWebApplication();
}

void SAIAssistantWebBrowser::WithWebApplication(
	TFunction<void(TSharedRef<FWebApplication>)>&& UsingWebApplication,
	bool bCheckUsingWebApplicationIsCalled) const
{
	UE::TUniqueLock WebApplicationLock(WebApplicationMutex);
	if (MaybeWebApplication.IsValid())
	{
		UsingWebApplication(MaybeWebApplication.ToSharedRef());
	}
	else
	{
		check(!bCheckUsingWebApplicationIsCalled);
	}
}

void SAIAssistantWebBrowser::LoadUrl(const FString& Url, EOpenBrowserMode Mode) const
{
	switch (Mode)
	{
		case EOpenBrowserMode::System:
		{
			FString ErrorString;
			FPlatformProcess::LaunchURL(*Url, TEXT(""), &ErrorString);
			if (!ErrorString.IsEmpty())
			{
				UE_LOG(
					LogAIAssistant, Error, 
					TEXT("%hs() - Could not open URL '%s' - '%s'."), __func__, *Url, *ErrorString);
			}
			break;
		}
		case EOpenBrowserMode::Embedded:
		{
			check(WebBrowserWidget);
			WebBrowserWidget->LoadURL(Url);
			break;
		}
	}
}

void SAIAssistantWebBrowser::ExecuteJavaScript(const FString& JavaScript)
{
	check(WebBrowserWidget);
	WebBrowserWidget->ExecuteJavascript(JavaScript);
}

void SAIAssistantWebBrowser::BindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	check(WebBrowserWidget);
	WebBrowserWidget->BindUObject(Name, Object, bIsPermanent);
}

void SAIAssistantWebBrowser::UnbindUObject(const FString& Name, UObject* Object, bool bIsPermanent)
{
	check(WebBrowserWidget);
	WebBrowserWidget->UnbindUObject(Name, Object, bIsPermanent);
}

FReply SAIAssistantWebBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Handled();
}

FReply SAIAssistantWebBrowser::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return FReply::Handled();
}

void SAIAssistantWebBrowser::CreateConversation()
{
	WithWebApplication(
		[](TSharedRef<FWebApplication> WebApplication) -> void
		{
			WebApplication->CreateConversation();
		});
}

void SAIAssistantWebBrowser::AddUserMessageToConversation(
	const FString& VisiblePrompt, const FString& HiddenContext)
{
	WithWebApplication(
		[&VisiblePrompt, &HiddenContext](TSharedRef<FWebApplication> WebApplication) -> void
		{
			WebApplication->AddUserMessageToConversation(
				FWebApplication::CreateUserMessage(VisiblePrompt, HiddenContext));
		});
}

SAIAssistantWebConnectionWidget::EConnectionState SAIAssistantWebBrowser::GetConnectionState() const
{
	SAIAssistantWebConnectionWidget::EConnectionState ConnectionState =
		SAIAssistantWebConnectionWidget::EConnectionState::Reconnecting;
	WithWebApplication(
		[&ConnectionState](TSharedRef<FWebApplication> WebApplication) -> void
		{
			switch (WebApplication->GetLoadState())
			{
			case FWebApplication::ELoadState::NotLoaded:
				ConnectionState =
					SAIAssistantWebConnectionWidget::EConnectionState::Reconnecting;
				break;
			case FWebApplication::ELoadState::Error:
				ConnectionState =
					SAIAssistantWebConnectionWidget::EConnectionState::Disconnected;
				break;
			case FWebApplication::ELoadState::Complete:
				ConnectionState =
					SAIAssistantWebConnectionWidget::EConnectionState::Connected;
				break;
			}
		},
		false);
	return ConnectionState;
}

#undef LOCTEXT_NAMESPACE
