// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHomeScreen.h"

#include "DesktopPlatformModule.h"
#include "Dialogs/SOutputLogDialog.h"
#include "Editor.h"
#include "Frame/MainFrameActions.h"
#include "Frame/RootWindowLocation.h"
#include "GameProjectGenerationModule.h"
#include "GameProjectUtils.h"
#include "HomeScreenMenuContext.h"
#include "Settings/HomeScreenSettings.h"
#include "HomeScreenWeb.h"
#include "HttpModule.h"
#include "HttpRetrySystem.h"
#include "IWebBrowserWindow.h"
#include "MainFrameModule.h"
#include "ProjectDescriptor.h"
#include "Settings/EditorSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "SWebBrowser.h"
#include "TimerManager.h"
#include "ToolMenus.h"
#include "UnrealEdGlobals.h"
#include "Internationalization/Culture.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "SHomeScreen"

SHomeScreen::SHomeScreen()
{
	if (!HttpRetryManager.IsValid())
	{
		HttpRetryManager = MakeShared<FHttpRetrySystem::FManager>(
			FHttpRetrySystem::FRetryLimitCountSetting(3),
			FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(3));
	}

	if (GEditor && GEditor->IsTimerManagerValid())
	{
		constexpr float TimerManagerCallRate = 5.f;
		constexpr bool bLoop = true;

		GEditor->GetTimerManager()->SetTimer(
			CheckInternetConnectionTimerHandle,
			FTimerDelegate::CreateLambda([this] ()
				{
					if (bIsRequestFinished)
					{
						if (bIsConnected || bForceRetry)
						{
							bForceRetry = false;
							TimerManagerCountOnceDisconnected = 0;
							CheckInternetConnection();
						}
						else
						{
							// When disconnected retry connection less times than when you are connected
							TimerManagerCountOnceDisconnected += 1;
							bForceRetry = TimerManagerCountOnceDisconnected == MaxTimerManagerCountOnceDisconnectedBeforeRetry;
						}
					}
				}),
				TimerManagerCallRate,
				bLoop);
	}
}

SHomeScreen::~SHomeScreen()
{
	if (UHomeScreenSettings* HomeScreenSettings = GetMutableDefault<UHomeScreenSettings>())
	{
		HomeScreenSettings->OnLoadAtStartupChanged().RemoveAll(this);
	}

	if (GEditor && GEditor->IsTimerManagerValid())
	{
		GEditor->GetTimerManager()->ClearTimer(CheckInternetConnectionTimerHandle);
	}

	if (WebObject.IsValid())
	{
		WebObject->OnNavigationChanged().RemoveAll(this);
		WebObject->OnTutorialProjectRequested().RemoveAll(this);
	}

	if (HttpRetryRequest.IsValid())
	{
		HttpRetryRequest->OnProcessRequestComplete().Unbind();
		HttpRetryRequest.Reset();
	}

	HttpRetryManager.Reset();
}

void SHomeScreen::Construct(const FArguments& InArgs, const TSharedPtr<SWebBrowser> InWebBrowser, const TWeakObjectPtr<UHomeScreenWeb> InWebObject)
{
	WebBrowser = InWebBrowser;
	WebObject = InWebObject;

	AutoLoadProjectComboBoxSelection = EAutoLoadProject::HomeScreen;
	if (UHomeScreenSettings* HomeScreenSettings = GetMutableDefault<UHomeScreenSettings>())
	{
		HomeScreenSettings->OnLoadAtStartupChanged().AddSP(this, &SHomeScreen::OnLoadAtStartupSettingChanged);
		AutoLoadProjectComboBoxSelection = HomeScreenSettings->LoadAtStartup;
	}

	if (WebObject.IsValid())
	{
		WebObject->OnNavigationChanged().AddSP(this, &SHomeScreen::OnNavigateToSection);
		WebObject->OnTutorialProjectRequested().AddSP(this, &SHomeScreen::OnOpenGettingStartedProject);
	}

	bHasLatestEngineProjects = HasAlreadyLatestEngineProject();
	MainHomeSelection = bHasLatestEngineProjects ? EMainSectionMenu::Home : EMainSectionMenu::GettingStarted;

	// Fixed Button Size
	constexpr float SectionButtonCheckBoxHeight = 28.f;

	// Main Section CheckBoxes
	CreateMainSectionCheckBox(HomeCheckBox, EMainSectionMenu::Home, LOCTEXT("ProjectHomeHome", "Home"), FAppStyle::GetBrush("HomeScreen.Home"));
	CreateMainSectionCheckBox(NewsCheckBox, EMainSectionMenu::News, LOCTEXT("ProjectHomeNews", "News"), FAppStyle::GetBrush("HomeScreen.News"));
	CreateMainSectionCheckBox(GettingStartedCheckBox, EMainSectionMenu::GettingStarted, LOCTEXT("ProjectHomeGettingStarted", "Getting Started"), FAppStyle::GetBrush("HomeScreen.Rocket"));
	CreateMainSectionCheckBox(SampleProjectsCheckBox, EMainSectionMenu::SampleProjects, LOCTEXT("ProjectHomeSampleProjects", "Sample Projects"), FAppStyle::GetBrush("HomeScreen.Archive"));

	// Documentation and Community Buttons
	CreateResourceButtons(ForumsButton, TEXT("https://forums.unrealengine.com/categories?tag=unreal-engine"),
		LOCTEXT("ProjectHomeForumsButton", "Forums"), FAppStyle::GetBrush("HomeScreen.Forum"));
	CreateResourceButtons(DocumentationButton, TEXT("https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-6-documentation"),
		LOCTEXT("ProjectHomeDocumentation", "Documentation"), FAppStyle::GetBrush("HomeScreen.Documentation"));
	CreateResourceButtons(TutorialsButton, TEXT("https://dev.epicgames.com/community/unreal-engine/learning"),
		LOCTEXT("ProjectHomeTutorialsButton", "Tutorials"), FAppStyle::GetBrush("HomeScreen.Tutorial"));
	CreateResourceButtons(RoadmapButton, TEXT("https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/tabs/127-unreal-engine-5-7"),
		LOCTEXT("ProjectHomeRoadmapButton", "Roadmap"), FAppStyle::GetBrush("HomeScreen.Roadmap"));
	CreateResourceButtons(ReleaseNotesButton, TEXT("https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-6-release-notes"),
		LOCTEXT("ProjectHomeReleaseNotesButton", "Release Notes"), FAppStyle::GetBrush("HomeScreen.ReleaseNotes"));

	// Social Media Buttons
	TSharedPtr<SButton> FacebookButton;
	TSharedPtr<SButton> TwitterButton;
	TSharedPtr<SButton> YouTubeButton;
	TSharedPtr<SButton> TwitchButton;
	TSharedPtr<SButton> InstagramButton;

	CreateSocialMediaButtons(FacebookButton, TEXT("https://www.facebook.com/unrealengine"), FAppStyle::GetBrush("SocialMedia.Facebook"));
	CreateSocialMediaButtons(TwitterButton, TEXT("https://twitter.com/unrealengine"), FAppStyle::GetBrush("SocialMedia.Twitter"));
	CreateSocialMediaButtons(YouTubeButton, TEXT("https://www.youtube.com/c/unrealengine"), FAppStyle::GetBrush("SocialMedia.YouTube"));
	CreateSocialMediaButtons(TwitchButton, TEXT("https://www.twitch.tv/unrealengine"), FAppStyle::GetBrush("SocialMedia.Twitch"));
	CreateSocialMediaButtons(InstagramButton, TEXT("https://www.instagram.com/unrealengine/"), FAppStyle::GetBrush("SocialMedia.Instagram"));

	// Project Browser
	ProjectBrowser = FGameProjectGenerationModule::Get().CreateProjectBrowser(false, false);

	// Check for internet connection on startup
	CheckInternetConnection();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(COLOR("#101014FF"))
		.Padding(0.f)
		[
			SNew(SHorizontalBox)

			// New Project Section
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(8.f)
				[
					// Title Icon
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.f, 32.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("HomeScreen.UnrealLogo"))
					]

					// Create and MyFolder Buttons
					+ SVerticalBox::Slot()
					.AutoHeight()
					.MinHeight(SectionButtonCheckBoxHeight)
					.MaxHeight(SectionButtonCheckBoxHeight)
					.Padding(8.f, 0.f, 8.f, 8.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "HomeScreen.CreateNewProjectButton")
						.OnClicked(this, &SHomeScreen::OnCreateProjectDialog, /** bInAllowProjectOpening */false, /** bInAllowProjectCreation */true)
						.HAlign(HAlign_Center)
						.ContentPadding(0.f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.f, 0.f, 8.f, 0.f)
							[
									SNew(SImage)
									.Image(FAppStyle::GetBrush("HomeScreen.PlusCircle"))
									.ColorAndOpacity(FStyleColors::Black)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Font(FAppStyle::GetFontStyle("HomeScreen.Font.NewAndCreateButton"))
								.Text(LOCTEXT("ProjectHomeCreateNewProject", "New Project"))
							]
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.MinHeight(SectionButtonCheckBoxHeight)
					.MaxHeight(SectionButtonCheckBoxHeight)
					.Padding(8.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "HomeScreen.MyFolderButton")
						.OnClicked(this, &SHomeScreen::OnCreateProjectDialog, /** bInAllowProjectOpening */true, /** bInAllowProjectCreation */false)
						.HAlign(HAlign_Center)
						.ContentPadding(0.f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("HomeScreen.FolderOpen"))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Font(FAppStyle::GetFontStyle("HomeScreen.Font.NewAndCreateButton"))
								.Text(LOCTEXT("ProjectHomeMyProjects", "My Projects"))
							]
						]
					]

					// Main Home Section
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.f, 16.f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							HomeCheckBox.ToSharedRef()
						]
					
						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							NewsCheckBox.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							GettingStartedCheckBox.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							SampleProjectsCheckBox.ToSharedRef()
						]
					]

					// Documentation and Community Section
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.f, 0.f, 8.f, 4.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResourceAndCommunity", "Resources & Community"))
						.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultBoldSize"))
						.ColorAndOpacity(FStyleColors::White)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.f, 0.f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							ForumsButton.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							DocumentationButton.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							TutorialsButton.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							RoadmapButton.ToSharedRef()
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.MinHeight(SectionButtonCheckBoxHeight)
						.MaxHeight(SectionButtonCheckBoxHeight)
						[
							ReleaseNotesButton.ToSharedRef()
						]
					]

					// Load on Startup Section
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Bottom)
					.FillHeight(1.f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.Padding(16.f, 0.f, 16.f, 16.f)
						[
							SNew(SBorder)
							.Clipping(EWidgetClipping::ClipToBounds)
							.BorderImage(FAppStyle::GetBrush("HomeScreen.NoInternet.Border"))
							.Padding(16.f)
							.Visibility_Lambda([this]()
							{
								return bIsConnected ? EVisibility::Collapsed : EVisibility::Visible;
							})
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.f, 0.f, 0.f, 16.f)
								.HAlign(HAlign_Left)
								[
									SNew(SBox)
									.WidthOverride(24.f)
									.HeightOverride(24.f)
									[
										SNew(SWidgetSwitcher)
										.WidgetIndex(this, &SHomeScreen::GetNoInternetIconIndex)

										+ SWidgetSwitcher::Slot()
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("HomeScreen.NoInternet.Icon"))
										]

										+ SWidgetSwitcher::Slot()
										[
											SNew(SCircularThrobber)
											.NumPieces(8)
											.Period(1.2f)
											.Radius(12.f)
										]
									]
								]

								+ SVerticalBox::Slot()
								.FillHeight(0.5f)
								.Padding(0.f, 0.f, 0.f, 16.f)
								[
									SNew(STextBlock)
									.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultBoldSize"))
									.Text(LOCTEXT("HomeScreenNoInternet", "No Internet Connection"))
									.ColorAndOpacity(FStyleColors::White)
								]

								+ SVerticalBox::Slot()
								.FillHeight(1.f)
								[
									SNew(SButton)
									.HAlign(HAlign_Center)
									.ButtonStyle(FAppStyle::Get(), "HomeScreen.MyFolderButton")
									.OnClicked(this, &SHomeScreen::OnInternetConnectionRetried)
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
										.Text(LOCTEXT("HomeScreenReconnect", "Reconnect"))
									]
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(16.f, 0.f)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
							.Text(LOCTEXT("ProjectHomeLoad", "Load on Startup"))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(16.f, 8.f, 16.f, 36.f)
						[
							SAssignNew(AutoLoadProjectComboBox, SComboButton)
							.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("HomeScreen.ComboButton"))
							.MenuPlacement(EMenuPlacement::MenuPlacement_CenteredAboveAnchor)
							.HasDownArrow(true)
							.ContentPadding(FMargin(0.f ,2.f))
							.ButtonContent()
							[
								SNew(STextBlock)
								.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
								.Text(this, &SHomeScreen::GetAutoLoadProjectComboBoxLabelText)
								.ColorAndOpacity_Lambda([this] ()
								{
									if (AutoLoadProjectComboBox.IsValid() && AutoLoadProjectComboBox->IsHovered())
									{
										return FStyleColors::White;
									}
									return FStyleColors::Foreground;
								})
							]
							.MenuContent()
							[
								CreateComboButtonMenuContentWidget()
							]
						]

						// Social Media Section
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(46.f, 0.f, 46.f, 20.f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								FacebookButton.ToSharedRef()
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								TwitterButton.ToSharedRef()
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								YouTubeButton.ToSharedRef()
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								TwitchButton.ToSharedRef()
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								InstagramButton.ToSharedRef()
							]
						]
					]
				]
			]

			// Web Api
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this] ()
				{
					return MainHomeSelection == EMainSectionMenu::Home && bHasLatestEngineProjects ? /** Show Recent Projects */ 0 : /** Show just the WebPage */ 1;
				})

				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.Padding(0.f, 0.f, 0.f, 24.f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(0.125f)
						[
							SNew(SSpacer)
						]

						+ SHorizontalBox::Slot()
						.FillWidth(0.75f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.Padding(0.f, 64.f, 0.f, 16.f)
							[
								SNew(SHorizontalBox)
							
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.f, 0.f, 8.f, 0.f)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RecentProjects", "Recent Projects"))
									.Font(FAppStyle::GetFontStyle("HomeScreen.Font.RecentProject"))
									.ColorAndOpacity(FStyleColors::White)
								]
							
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "HomeScreen.SeeAllProjectsButton")
									.OnClicked(this, &SHomeScreen::OnCreateProjectDialog, /** bInAllowProjectOpening */true, /** bInAllowProjectCreation */false)
									.ContentPadding(FMargin(18, 10))
									[
										SNew(STextBlock)
										.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
										.Text(LOCTEXT("SeeAllProjects", "See All Projects"))
									]
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SBorder)
								.Clipping(EWidgetClipping::ClipToBounds)
								.BorderImage(FAppStyle::GetBrush("HomeScreen.RecentProjects.Background"))
								.Padding(24.f)
								[
									SNew(SVerticalBox)

									+ SVerticalBox::Slot()
									[
										SNew(SBox)
										.HeightOverride(172.f)
										.Padding(0.f)
										[
											ProjectBrowser.ToSharedRef()
										]
									]
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(0.125f)
						[
							SNew(SSpacer)
						]
					]

					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						WebBrowser.ToSharedRef()
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					WebBrowser.ToSharedRef()
				]
			]
		]
	];

	OnMainHomeSectionChanged(ECheckBoxState::Checked, MainHomeSelection);
}

FReply SHomeScreen::OnCreateProjectDialog(bool bInAllowProjectOpening, bool bInAllowProjectCreation)
{
	if (GUnrealEd->WarnIfLightingBuildIsCurrentlyRunning())
	{
		return FReply::Handled();
	}

	TSharedRef<SWindow> NewProjectWindow =
		FMainFrameActionCallbacks::CreateProjectBrowserWindow(bInAllowProjectOpening, bInAllowProjectCreation);

	NewProjectWindow->SetContent(FGameProjectGenerationModule::Get().CreateGameProjectDialog(bInAllowProjectOpening, bInAllowProjectCreation));

	IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	if (MainFrameModule.GetParentWindow().IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(NewProjectWindow, MainFrameModule.GetParentWindow().ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(NewProjectWindow);
	}

	return FReply::Handled();
}

FReply SHomeScreen::OnSocialMediaClicked(FString InURL)
{
	FPlatformProcess::LaunchURL(*InURL, nullptr, nullptr);
	return FReply::Handled();
}

ECheckBoxState SHomeScreen::IsMainHomeSectionChecked(EMainSectionMenu InMainHomeSelection) const
{
	return MainHomeSelection == InMainHomeSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SHomeScreen::OnMainHomeSectionChanged(ECheckBoxState InCheckBoxState, EMainSectionMenu InMainHomeSelection)
{
	FString URL = TEXT("");
	const FString BaseURL = TEXT("https://editor.unrealengine.com");
	// Use the Editor Language for the WebPage language.
	const FString FullLocale = FInternationalization::Get().GetCurrentLanguage()->GetName();
	switch (InMainHomeSelection)
	{
	case EMainSectionMenu::News:
		MainHomeSelection = EMainSectionMenu::News;
		URL = BaseURL / FullLocale / TEXT("news");
		break;
	case EMainSectionMenu::GettingStarted:
		MainHomeSelection = EMainSectionMenu::GettingStarted;
		URL = BaseURL / FullLocale / TEXT("get-started-with-unreal-engine");
		break;
	case EMainSectionMenu::SampleProjects:
		MainHomeSelection = EMainSectionMenu::SampleProjects;
		URL = BaseURL / FullLocale / TEXT("samples");
		break;
	case EMainSectionMenu::None:
	case EMainSectionMenu::Home:
	default:
		MainHomeSelection = EMainSectionMenu::Home;
		URL = bHasLatestEngineProjects ? BaseURL / FullLocale / TEXT("recent-projects") : BaseURL / FullLocale / TEXT("no-projects");
		break;
	}

	if (WebBrowser.IsValid())
	{
		WebBrowser->LoadURL(URL);
	}
}

void SHomeScreen::OnLoadAtStartupSettingChanged(EAutoLoadProject InAutoLoadOption)
{
	AutoLoadProjectComboBoxSelection = InAutoLoadOption;
}

FReply SHomeScreen::OnAutoLoadOptionChanged(EAutoLoadProject InAutoLoadOption)
{
	if (AutoLoadProjectComboBoxSelection == InAutoLoadOption)
	{
		return FReply::Handled();
	}

	// Do not change the option if we can't change the setting otherwise they are not synced, the PostEditChangeProperty will handle the call to set our ComboBoxSelection.
	if (UHomeScreenSettings* HomeScreenSettings = GetMutableDefault<UHomeScreenSettings>())
	{
		HomeScreenSettings->LoadAtStartup = InAutoLoadOption;

		// Manually trigger the property changed event so that the home screen editor settings UI updates to reflect the change
		FProperty* LoadAtStartupProperty = FindFProperty<FProperty>(HomeScreenSettings->GetClass(), "LoadAtStartup");
		if (LoadAtStartupProperty != NULL)
		{
			FPropertyChangedEvent PropertyUpdateStruct(LoadAtStartupProperty);
			HomeScreenSettings->PostEditChangeProperty(PropertyUpdateStruct);
		}
	}

	if (TSharedPtr<SWidget> ComboButtonMenu = ComboButtonMenuWeak.Pin())
	{
		FSlateApplication::Get().DismissMenuByWidget(ComboButtonMenu.ToSharedRef());
	}

	return FReply::Handled();
}

EVisibility SHomeScreen::IsAutoLoadOptionCheckVisible(EAutoLoadProject InAutoLoadOption) const
{
	return AutoLoadProjectComboBoxSelection == InAutoLoadOption ? EVisibility::Visible : EVisibility::Hidden;
}

bool SHomeScreen::IsCheckBoxCheckedOrHovered(const TSharedPtr<SCheckBox> InCheckBox) const
{
	return InCheckBox.IsValid() ? InCheckBox->IsHovered() || InCheckBox->IsChecked() : false;
}

FSlateColor SHomeScreen::GetMainSectionCheckBoxColor(const TSharedPtr<SCheckBox> InCheckBox) const
{
	return IsCheckBoxCheckedOrHovered(InCheckBox) ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.65f);
}

FSlateColor SHomeScreen::GetResourceAndSocialMediaButtonColor(const TSharedPtr<SButton> InButton) const
{
	if (InButton.IsValid() && InButton->IsHovered())
	{
		return FLinearColor::White;
	}

	return FLinearColor(1.f, 1.f, 1.f, 0.65f);
}

void SHomeScreen::CreateMainSectionCheckBox(TSharedPtr<SCheckBox>& OutCheckBox, EMainSectionMenu InMainHomeSelection, const FText& InText, const FSlateBrush* InImage)
{
	OutCheckBox = SNew(SCheckBox)
		.IsEnabled(this, &SHomeScreen::IsMainSectionEnabled, InMainHomeSelection)
		.Style(FAppStyle::Get(), "HomeScreen.MainMenuSectionButton")
		.IsChecked(this, &SHomeScreen::IsMainHomeSectionChecked, InMainHomeSelection)
		.OnCheckStateChanged(this, &SHomeScreen::OnMainHomeSectionChanged, InMainHomeSelection)
		.Padding(FMargin(8.f, 2.f));

	OutCheckBox->SetContent(
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SImage)
			.Image(InImage)
			.ColorAndOpacity(this, &SHomeScreen::GetMainSectionCheckBoxColor, OutCheckBox)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
			.Text(InText)
			.ColorAndOpacity(this, &SHomeScreen::GetMainSectionCheckBoxColor, OutCheckBox)
		]);
}

void SHomeScreen::CreateResourceButtons(TSharedPtr<SButton>& OutButton, FString InLink, const FText& InText, const FSlateBrush* InImage)
{
	SAssignNew(OutButton, SButton)
	.IsEnabled(this, &SHomeScreen::IsConnectedToInternet)
	.ButtonStyle(FAppStyle::Get(), "NoBorder")
	.ContentPadding(FMargin(0.f, 2.f))
	.OnClicked(this, &SHomeScreen::OnSocialMediaClicked, InLink);

	OutButton->SetContent(
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SImage)
			.Image(InImage)
			.ColorAndOpacity(this, &SHomeScreen::GetResourceAndSocialMediaButtonColor, OutButton)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
			.Text(InText)
			.ColorAndOpacity(this, &SHomeScreen::GetResourceAndSocialMediaButtonColor, OutButton)
		]);
}

void SHomeScreen::CreateSocialMediaButtons(TSharedPtr<SButton>& OutButton, FString InLink, const FSlateBrush* InImage)
{
	SAssignNew(OutButton, SButton)
	.ButtonStyle(FAppStyle::Get(), "NoBorder")
	.OnClicked(this, &SHomeScreen::OnSocialMediaClicked, InLink);

	OutButton->SetContent(
			SNew(SImage)
			.Image(InImage)
			.ColorAndOpacity(this, &SHomeScreen::GetResourceAndSocialMediaButtonColor, OutButton));
}

TSharedRef<SWidget> SHomeScreen::CreateComboButtonMenuContentWidget()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered("HomeScreenComboButtonMenu"))
	{
		FToolMenuContext HomeScreenComboContext;

		UHomeScreenContext* HomeScreenContextObject = NewObject<UHomeScreenContext>();
		HomeScreenContextObject->HomeScreen = SharedThis(this);

		HomeScreenComboContext.AddObject(HomeScreenContextObject);
		TSharedRef<SWidget> ComboButtonMenuRef = ToolMenus->GenerateWidget("HomeScreenComboButtonMenu", HomeScreenComboContext);
		ComboButtonMenuWeak = ComboButtonMenuRef;
		return ComboButtonMenuRef;
	}
	return SNullWidget::NullWidget;
}

void SHomeScreen::CheckInternetConnection()
{
	// Fire another request only if the previous one finished
	if (HttpRetryRequest.IsValid())
	{
		return;
	}

	bIsRequestFinished = false;

	HttpRetryRequest = HttpRetryManager->CreateRequest(
		FHttpRetrySystem::FRetryLimitCountSetting(3),
		FHttpRetrySystem::FRetryTimeoutRelativeSecondsSetting(3),
		FHttpRetrySystem::FRetryResponseCodes(),
		FHttpRetrySystem::FRetryVerbs(),
		FHttpRetrySystem::FRetryDomainsPtr(),
		FHttpRetrySystem::FRetryLimitCountSetting(3),
		FHttpRetrySystem::FExponentialBackoffCurve());

	HttpRetryRequest->SetURL(TEXT("https://www.google.com/generate_204"));
	HttpRetryRequest->SetVerb("GET");

	HttpRetryRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			bIsRequestFinished = true;
			bIsConnected = bWasSuccessful;
			HttpRetryRequest.Reset();
		});

	HttpRetryRequest->ProcessRequest();
}

bool SHomeScreen::IsConnectedToInternet() const
{
	return bIsConnected;
}

bool SHomeScreen::IsMainSectionEnabled(EMainSectionMenu InHomeSection) const
{
	return IsConnectedToInternet() || InHomeSection == EMainSectionMenu::Home;
}

int32 SHomeScreen::GetNoInternetIconIndex() const
{
	return bIsRequestFinished? 0 /** No Internet */ : 1 /** Retrying */;
}

void SHomeScreen::OnNavigateToSection(EMainSectionMenu InSectionToNavigate)
{
	OnMainHomeSectionChanged(ECheckBoxState::Checked, InSectionToNavigate);
}

void SHomeScreen::OnOpenGettingStartedProject()
{
	FString TemplateRootFolders = FPaths::RootDir() + TEXT("Templates");
	FString UEIntroRootFolder = TemplateRootFolders / TEXT("TP_UEIntro_BP");
	const FString SearchString = UEIntroRootFolder / TEXT("*.") + FProjectDescriptor::GetExtension();
	TArray<FString> FoundProjectFiles;
	IFileManager::Get().FindFiles(FoundProjectFiles, *SearchString, /*Files=*/true, /*Directories=*/false);

	// There should be only 1 match
	if (FoundProjectFiles.Num() != 1)
	{
		return;
	}

	const FString TemplateProjectPath = UEIntroRootFolder / FoundProjectFiles[0];
	const FString DesiredProjectName = TEXT("UEIntroProject");

	// Get the default project creation folder
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	FString DefaultProjectFolder = DesktopPlatform->GetDefaultProjectCreationPath();

	// Find a unique name
	FString ProjectName = DesiredProjectName;
	FString NewProjectFolder = FPaths::Combine(DefaultProjectFolder, ProjectName);
	int32 Counter = 1;
	constexpr int32 MaxTries = 1000;

	while (FPaths::DirectoryExists(NewProjectFolder))
	{
		ProjectName = FString::Printf(TEXT("%s_%d"), *DesiredProjectName, Counter++);
		NewProjectFolder = FPaths::Combine(DefaultProjectFolder, ProjectName);
		if (Counter > MaxTries)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("TooManyTriesToFindAUniqueName", "Something went wrong when trying to find a unique name for the tutorial project.\nCheck your folder containing the projects and clean that up of unneeded tutorial projects."));
			return;
		}
	}

	const FString ProjectPath = DefaultProjectFolder;
	const FString Filename = ProjectName + TEXT(".") + FProjectDescriptor::GetExtension();
	FString ProjectFilename = FPaths::Combine(*ProjectPath, *ProjectName, *Filename);
	FPaths::MakePlatformFilename(ProjectFilename);

	FText FailReason, FailLog;
	FProjectInformation ProjectInfo;
	ProjectInfo.TemplateCategory = TEXT("Game");
	ProjectInfo.TemplateFile = TemplateProjectPath;
	ProjectInfo.ProjectFilename = ProjectFilename;

	if (!FGameProjectGenerationModule::Get().CreateProject(ProjectInfo, FailReason, FailLog))
	{
		SOutputLogDialog::Open(LOCTEXT("CreateProject", "Create Project"), FailReason, FailLog, FText::GetEmpty());
		return;
	}

	// Successfully created the project. Update the last created location string.
	FString CreatedProjectPath = FPaths::GetPath(FPaths::GetPath(ProjectFilename));

	// If the original path was the drives root (ie: C:/) the double path call strips the last /
	if (CreatedProjectPath.EndsWith(":"))
	{
		CreatedProjectPath.AppendChar('/');
	}

	UEditorSettings* Settings = GetMutableDefault<UEditorSettings>();
	Settings->CreatedProjectPaths.Remove(CreatedProjectPath);
	Settings->CreatedProjectPaths.Insert(CreatedProjectPath, 0);
	Settings->PostEditChange();

	// Open the project
	FText FailReasonOpen;
	if (FGameProjectGenerationModule::Get().OpenProject(ProjectFilename, FailReasonOpen))
	{
		// Successfully opened the project, the editor is closing.
		// Close this window in case something prevents the editor from closing (save dialog, quit confirmation, etc)
		if (FApp::HasProjectName())
		{
			TSharedPtr<SWindow> ContainingWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	
			if (ContainingWindow.IsValid())
			{
				ContainingWindow->RequestDestroyWindow();
			}
		}
	}
	else
	{
		FString ErrorString = FailReasonOpen.ToString();
		UE_LOG(LogTemp, Log, TEXT("%s"), *ErrorString);
		FMessageDialog::Open(EAppMsgType::Ok, FailReasonOpen);
	}
}

FText SHomeScreen::GetAutoLoadProjectComboBoxLabelText() const
{
	return AutoLoadProjectComboBoxSelection == EAutoLoadProject::HomeScreen ? LOCTEXT("HomeScreenComboHomePanel", "Home Panel") : LOCTEXT("HomeScreenComboMostRecentProject", "Most Recent Project");
}

FReply SHomeScreen::OnInternetConnectionRetried()
{
	CheckInternetConnection();
	return FReply::Handled();
}

bool SHomeScreen::HasAlreadyLatestEngineProject() const
{
	TArray<FString> ProjectFiles;

	FString RootDir;
	IDesktopPlatform* DesktopPlatformModule = FDesktopPlatformModule::Get();
	if (!DesktopPlatformModule)
	{
		return false;
	}

	const FString EngineIdentifier = DesktopPlatformModule->GetCurrentEngineIdentifier();

	DesktopPlatformModule->EnumerateProjectsKnownByEngine(EngineIdentifier, false, ProjectFiles);
	if (!DesktopPlatformModule->GetEngineRootDirFromIdentifier(EngineIdentifier, RootDir))
	{
		// The engine root dir couldn't be found so fake that it's a new user
		return false;
	}

	ProjectFiles.RemoveAll([RootDir, DesktopPlatformModule] (const FString& InProjectFile)
		{
			FString ProjectFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*InProjectFile);
			if (!FPaths::FileExists(ProjectFilename))
			{
				return true;
			}

			if (ProjectFilename.Contains(RootDir))
			{
				return true;
			}
			FString Identifier;
			DesktopPlatformModule->GetEngineIdentifierForProject(ProjectFilename, Identifier);

			FEngineVersion ProjectEngineVersion;

			if (DesktopPlatformModule->IsStockEngineRelease(Identifier))
			{
				DesktopPlatformModule->TryParseStockEngineVersion(Identifier, ProjectEngineVersion);
			}

			if (ProjectEngineVersion.IsEmpty())
			{
				FString RootProjectDir;
				DesktopPlatformModule->GetEngineRootDirFromIdentifier(Identifier, RootProjectDir);
				if (!DesktopPlatformModule->TryGetEngineVersion(RootProjectDir, ProjectEngineVersion))
				{
					return true;
				}
			}

			const FString EngineVersionString = FEngineVersion::Current().ToString(EVersionComponent::Patch);
			const FString ProjectEngineVersionString = ProjectEngineVersion.ToString(EVersionComponent::Patch);

			if (EngineVersionString != ProjectEngineVersionString)
			{
				return true;
			}

			return false;
		});

	return !ProjectFiles.IsEmpty();
}

FDelayedAutoRegisterHelper SHomeScreen::LoadStartupComboButtonRegistration(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			static const FName LoadStartupComboButtonName("HomeScreenComboButtonMenu");
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(LoadStartupComboButtonName);
			FToolMenuSection& Section = Menu->AddSection("StartupSection");

			Section.AddDynamicEntry(
				TEXT("HomeTabDynamic"),
				FNewToolMenuSectionDelegate::CreateLambda([] (FToolMenuSection& InSection)
					{
						if (UHomeScreenContext* Context = InSection.FindContext<UHomeScreenContext>())
						{
							if (const TSharedPtr<SHomeScreen> HomeScreen = Context->HomeScreen.Pin())
							{
								const TSharedRef<SHomeScreen> HomeScreenRef = HomeScreen.ToSharedRef();

								TSharedRef<SButton> HomeTabWidget = SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "HomeScreen.ComboButton.MenuButton")
									.ContentPadding(FMargin(12.f, 10.f))
									.OnClicked(HomeScreenRef, &SHomeScreen::OnAutoLoadOptionChanged, EAutoLoadProject::HomeScreen)
									[
										SNew(SHorizontalBox)

										+ SHorizontalBox::Slot()
										.Padding(0.f, 0.f, 8.f, 0.f)
										.AutoWidth()
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.Check"))
											.Visibility(HomeScreenRef, &SHomeScreen::IsAutoLoadOptionCheckVisible, EAutoLoadProject::HomeScreen)
										]

										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(STextBlock)
											.Text(LOCTEXT("HomeScreenHomePanel", "Home Panel"))
											.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
											.ColorAndOpacity(FStyleColors::White)
										]
									];

								InSection.AddEntry(FToolMenuEntry::InitWidget(
									TEXT("HomeTab"),
									SNew(SBox).Padding(8.f, 0.f, 8.f, 4.f)[HomeTabWidget],
									FText::GetEmpty()));
							}
						}
					})
				);


			Section.AddDynamicEntry(
				TEXT("MostRecentProjectDynamic"),
				FNewToolMenuSectionDelegate::CreateLambda([] (FToolMenuSection& InSection)
					{
						if (UHomeScreenContext* Context = InSection.FindContext<UHomeScreenContext>())
						{
							if (const TSharedPtr<SHomeScreen> HomeScreen = Context->HomeScreen.Pin())
							{
								const TSharedRef<SHomeScreen> HomeScreenRef = HomeScreen.ToSharedRef();

								TSharedRef<SButton> MostRecentProjectWidget = SNew(SButton)
									.ButtonStyle(FAppStyle::Get(), "HomeScreen.ComboButton.MenuButton")
									.ContentPadding(FMargin(12.f, 10.f))
									.OnClicked(HomeScreenRef, &SHomeScreen::OnAutoLoadOptionChanged, EAutoLoadProject::LastProject)
									[
										SNew(SHorizontalBox)

										+ SHorizontalBox::Slot()
										.Padding(0.f, 0.f, 8.f, 0.f)
										.AutoWidth()
										[
											SNew(SImage)
											.Image(FAppStyle::GetBrush("Icons.Check"))
											.Visibility(HomeScreenRef, &SHomeScreen::IsAutoLoadOptionCheckVisible, EAutoLoadProject::LastProject)
										]

										+ SHorizontalBox::Slot()
										.AutoWidth()
										[
											SNew(STextBlock)
											.Text(LOCTEXT("HomeScreenMostRecentProject", "Most Recent Project"))
											.Font(FAppStyle::GetFontStyle("HomeScreen.Font.DefaultSize"))
											.ColorAndOpacity(FStyleColors::White)
										]
									];

								InSection.AddEntry(FToolMenuEntry::InitWidget(
									TEXT("MostRecentProject"),
									SNew(SBox).Padding(8.f, 0.f)[MostRecentProjectWidget],
									FText::GetEmpty()));
							}
						}
					})
				);
		}
);

#undef LOCTEXT_NAMESPACE
