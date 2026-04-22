// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Engine/TimerHandle.h"
#include "Settings/HomeScreenCommon.h"
#include "HomeScreenWeb.h"
#include "HttpRetrySystem.h"
#include "Input/Reply.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SCheckBox;
class SComboButton;
class SWebBrowser;
template <typename ItemType> class STileView;

class SHomeScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHomeScreen)
	{}

	SLATE_END_ARGS()

	SHomeScreen();
	virtual ~SHomeScreen() override;

	/** Constructs the HomeScreen widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<SWebBrowser> InWebBrowser, const TWeakObjectPtr<UHomeScreenWeb> InWebObject);

private:
	/** Opens the Create Project dialog */
	FReply OnCreateProjectDialog(bool bInAllowProjectOpening, bool bInAllowProjectCreation);

	/** Handles social media button clicks */
	FReply OnSocialMediaClicked(FString InURL);

	/** Checks if a main home section is selected */
	ECheckBoxState IsMainHomeSectionChecked(EMainSectionMenu InMainHomeSelection) const;

	/** Handles changes to the main home section */
	void OnMainHomeSectionChanged(ECheckBoxState InCheckBoxState, EMainSectionMenu InMainHomeSelection);

	/** Callback from the HomeScreenSettings when the LoadAtStartup property change */
	void OnLoadAtStartupSettingChanged(EAutoLoadProject InAutoLoadOption);

	/** Handles changes to autoload last project selection */
	FReply OnAutoLoadOptionChanged(EAutoLoadProject InAutoLoadOption);

	/** True if the current AutoLoad option match the given InAutoLoadOption */
	EVisibility IsAutoLoadOptionCheckVisible(EAutoLoadProject InAutoLoadOption) const;

	/** Checks if a checkbox is checked or hovered */
	bool IsCheckBoxCheckedOrHovered(const TSharedPtr<SCheckBox> InCheckBox) const;

	/** Gets the color of the main section checkbox icon and text */
	FSlateColor GetMainSectionCheckBoxColor(const TSharedPtr<SCheckBox> InCheckBox) const;

	/** Gets the color of resource and social media section icon and text */
	FSlateColor GetResourceAndSocialMediaButtonColor(const TSharedPtr<SButton> InButton) const;

	/** Creates a main section checkboxes */
	void CreateMainSectionCheckBox(TSharedPtr<SCheckBox>& OutCheckBox, EMainSectionMenu InMainHomeSelection, const FText& InText, const FSlateBrush* InImage);

	/** Creates resource buttons */
	void CreateResourceButtons(TSharedPtr<SButton>& OutButton, FString InLink, const FText& InText, const FSlateBrush* InImage);

	/** Creates social media buttons */
	void CreateSocialMediaButtons(TSharedPtr<SButton>& OutButton, FString InLink, const FSlateBrush* InImage);

	/** Creates the content widget for the combo button menu */
	TSharedRef<SWidget> CreateComboButtonMenuContentWidget();

	/** Checks the internet connection */
	void CheckInternetConnection();

	/** Returns whether the system is connected to the internet */
	bool IsConnectedToInternet() const;

	/** Returns whether the main section is enabled */
	bool IsMainSectionEnabled(EMainSectionMenu InHomeSection) const;

	/** Gets the index of the SWidgetSwitcher for the no internet icon, either NoInternet or Loading when retrying */
	int32 GetNoInternetIconIndex() const;

	/** Handles navigation to a section from a web request */
	void OnNavigateToSection(EMainSectionMenu InSectionToNavigate);

	/** Handles the getting started template project creation */
	void OnOpenGettingStartedProject();

	/** Gets the label text for the autoload project combo box */
	FText GetAutoLoadProjectComboBoxLabelText() const;

	/** Executed when clicking on the Reconnect button when no internet connection is detected */
	FReply OnInternetConnectionRetried();

	/** Whether the current user already created project with this Engine version */
	bool HasAlreadyLatestEngineProject() const;

private:
	/** Registers the LoadStartup ComboButton Menu */
	static FDelayedAutoRegisterHelper LoadStartupComboButtonRegistration;

private:
	/** Currently selected main home section */
	EMainSectionMenu MainHomeSelection = EMainSectionMenu::Home;

	/** Main section checkboxes */
	TSharedPtr<SCheckBox> HomeCheckBox;
	TSharedPtr<SCheckBox> NewsCheckBox;
	TSharedPtr<SCheckBox> GettingStartedCheckBox;
	TSharedPtr<SCheckBox> SampleProjectsCheckBox;

	/** Resource section buttons */
	TSharedPtr<SButton> ForumsButton;
	TSharedPtr<SButton> DocumentationButton;
	TSharedPtr<SButton> TutorialsButton;
	TSharedPtr<SButton> RoadmapButton;
	TSharedPtr<SButton> ReleaseNotesButton;

	/** Http retry system used to check Internet connection */
	TSharedPtr<FHttpRetrySystem::FManager> HttpRetryManager;
	TSharedPtr<FHttpRetrySystem::FRequest> HttpRetryRequest;

	/** Combo box for autoload project selection */
	TSharedPtr<SComboButton> AutoLoadProjectComboBox;

	/** Widget for project browser */
	TSharedPtr<SWidget> ProjectBrowser;

	/** Current combo button menu */
	TWeakPtr<SWidget> ComboButtonMenuWeak;

	/** Timer handle for checking internet connection */
	FTimerHandle CheckInternetConnectionTimerHandle;

	/** Path of the currently selected project */
	FString CurrentSelectedProjectPath;

	/** Whether the system is connected to the internet */
	bool bIsConnected = true;

	/** Whether the last request has finished */
	bool bIsRequestFinished = true;

	/** Whether the current user already created project with this Engine version */
	bool bHasLatestEngineProjects = false;

	/** How many times we executed the function without being connected */
	int32 TimerManagerCountOnceDisconnected = 0;

	/** Max TimerManager function execution before retrying once disconnected */
	int32 MaxTimerManagerCountOnceDisconnectedBeforeRetry = 5;

	/** Whether to force a retry on the connection */
	bool bForceRetry = false;

	/** Selection for autoload project combo box */
	EAutoLoadProject AutoLoadProjectComboBoxSelection;

	/** WebBrowser widget passed down from the MainFrameModule */
	TSharedPtr<SWebBrowser> WebBrowser;
	
	/** WebBrowser object passed down from the MainFrameModule */
	TWeakObjectPtr<UHomeScreenWeb> WebObject;
};
