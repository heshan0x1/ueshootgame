// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LevelViewportActions.h"
#include "MediaOutput.h"
#include "MediaProfileEditor.h"
#include "MediaProfileEditorUserSettings.h"
#include "MediaSource.h"
#include "PropertyEditorModule.h"
#include "SMediaProfileViewport.h"
#include "ToolMenus.h"
#include "ToolMenusEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Profile/MediaProfile.h"
#include "Styling/SlateIconFinder.h"
#include "Tests/ToolMenusTestUtilities.h"
#include "UI/MediaFrameworkTimecodeGenlockToolMenuEntry.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"

#include "SMediaProfileMediaItemDisplay.generated.h"

class UMediaTexture;
class UMediaProfile;

/**
 * Used as the context object for the viewport toolbar displayed at the top of each display, stores a reference to the viewport widget
 * that is displaying the toolbar instance
 */
UCLASS()
class UMediaProfileMediaItemDisplayContext : public UObject
{
	GENERATED_BODY()

public:
	template<typename T>
	TSharedPtr<T> GetDisplayWidget() const
	{
		if (DisplayWidget.IsValid())
		{
			return StaticCastSharedPtr<T>(DisplayWidget.Pin());
		}

		return nullptr;
	}
	
	template<typename T>
	void SetDisplayWidget(const TSharedPtr<T>& InDisplayWidget)
	{
		DisplayWidget = InDisplayWidget;
	}
	
private:
	TWeakPtr<SWidget> DisplayWidget;
};

#define LOCTEXT_NAMESPACE "SMediaProfileMediaItemDisplay"

/** Widget to display the input or capture from a single media source or output */
template<typename TMediaItem>
class SMediaProfileMediaItemDisplayBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMediaProfileMediaItemDisplayBase) {}
		SLATE_ARGUMENT(TWeakPtr<FMediaProfileEditor>, MediaProfileEditor)
		SLATE_ARGUMENT(int32, PanelIndex)
		SLATE_ARGUMENT(int32, MediaItemIndex)
	SLATE_END_ARGS()
		
	void Construct(const FArguments& InArgs, const TSharedPtr<SMediaProfileViewport>& InOwningViewport)
	{
		OwningViewport = InOwningViewport;
		MediaProfileEditor = InArgs._MediaProfileEditor;
		MediaItemIndex = InArgs._MediaItemIndex;
		PanelIndex = InArgs._PanelIndex;

		TimecodeGenlockToolMenuEntry = MakeShared<FMediaFrameworkTimecodeGenlockToolMenuEntry>(GetMediaProfile());
		
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SMediaProfileMediaItemDisplayBase::OnObjectPropertyChanged);

		CommandList = MakeShared<FUICommandList>();
		BindCommands();
		
		ChildSlot
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						CreateToolbar()
					]
				]
			]

			+SVerticalBox::Slot()
			[
				SAssignNew(Overlay, SOverlay)

				+SOverlay::Slot()
				[
					SAssignNew(MediaImageContainer, SBorder)
					.BorderBackgroundColor(FLinearColor::Black)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				]
			]
		];

		ConfigureMediaImage();
	}

	virtual ~SMediaProfileMediaItemDisplayBase() override
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (CommandList->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }
	
protected:
	/** Performs any needed configuration to output the media item to the UI */
	virtual void ConfigureMediaImage() = 0;

	/** Gets the typed media item being displayed */
	virtual TMediaItem* GetMediaItem() const = 0;

	/** Gets the label to display for the media item */
	virtual FText GetMediaItemLabel() const = 0;

	/** Gets the label for the base type of the media item (e.g. media source/media output) */
	virtual FText GetBaseMediaTypeLabel() const = 0;

	UMediaProfile* GetMediaProfile() const
	{
		UMediaProfile* MediaProfile = nullptr;
		if (MediaProfileEditor.IsValid())
		{
			MediaProfile = MediaProfileEditor.Pin()->GetMediaProfile();
		}
		
		return MediaProfile;
	}
	
	/** Allows implementations to add their own entries to the viewport toolbar */
	virtual void AddToolbarEntries(FToolMenuSection& Section) { }

	/** Allows implementations to add their own command lists to the viewport toolbar */
	virtual void AppendToolbarCommandList(FToolMenuContext& Context) { }

	/** Binds commands to the display's command list */
	void BindCommands()
	{
		CommandList->MapAction(
			FLevelViewportCommands::Get().ToggleImmersive,
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
					if (!PinnedOwningViewport.IsValid())
					{
						return;
					}
						
					if (!IsImmersive())
					{
						PinnedOwningViewport->SetImmersivePanel(PanelIndex);
					}
					else
					{
						PinnedOwningViewport->ClearImmersivePanel();
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this] { return IsImmersive(); })
			));

		CommandList->MapAction(FLevelViewportCommands::Get().ToggleMaximize,
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
					if (!PinnedOwningViewport.IsValid())
					{
						return;
					}
						
					if (!IsMaximized())
					{
						PinnedOwningViewport->MaximizePanel(PanelIndex);
					}
					else
					{
						PinnedOwningViewport->RestorePreviousLayout();
					}
				}),
				FCanExecuteAction::CreateLambda([this]()
				{
					TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
					if (!PinnedOwningViewport.IsValid())
					{
						return false;
					}

					return IsMaximized() ? PinnedOwningViewport->CanRestorePreviousLayout() : PinnedOwningViewport->CanMaximizePanel(PanelIndex);
				})
			));
	}

	/** Creates the viewport display's toolbar */
	TSharedRef<SWidget> CreateToolbar()
	{
		const FName ViewportToolbarName = "MediaProfileEditor.ViewportToolbar";

		if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
		{
			UToolMenu* ToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
			ToolbarMenu->StyleName = "ViewportToolbar";

			// Need empty left section to ensure the middle and right sections get positioned correctly
			FToolMenuSection& LeftSection = ToolbarMenu->AddSection("Left");
			LeftSection.Alignment = EToolMenuSectionAlign::First;
			
			LeftSection.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				UMediaProfileMediaItemDisplayContext* Context = Section.FindContext<UMediaProfileMediaItemDisplayContext>();
				TSharedPtr<SMediaProfileMediaItemDisplayBase<TMediaItem>> ContextWidget = Context->GetDisplayWidget<SMediaProfileMediaItemDisplayBase<TMediaItem>>();
				
				if (!ContextWidget.IsValid())
				{
					return;
				}

				FToolMenuEntry TimecodeEntry = ContextWidget->TimecodeGenlockToolMenuEntry->CreateTimecodeToolMenuEntry(TAttribute<bool>::CreateLambda([]
				{
					if (const UMediaProfileEditorUserSettings* UserSettings = GetDefault<UMediaProfileEditorUserSettings>())
					{
						return UserSettings->bShowTimecodeInViewportToolbar;
					}

					return true;
				}));

				//TODO: Hidden for now until per-source timecode can be figured out (see UE-UE-305891)
				//Section.AddEntry(TimecodeEntry);
				
				FToolMenuEntry GenlockEntry = ContextWidget->TimecodeGenlockToolMenuEntry->CreateGenlockToolMenuEntry(TAttribute<bool>::CreateLambda([]
				{
					if (const UMediaProfileEditorUserSettings* UserSettings = GetDefault<UMediaProfileEditorUserSettings>())
					{
						return UserSettings->bShowGenlockInViewportToolbar;
					}

					return true;
				}));

				//TODO: Hidden for now until per-source genlock can be figured out (see UE-UE-305891)
				//Section.AddEntry(GenlockEntry);
			}));
			
			FToolMenuSection& RightSection = ToolbarMenu->AddSection("Right");
			RightSection.Alignment = EToolMenuSectionAlign::Last;

			RightSection.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				UMediaProfileMediaItemDisplayContext* Context = Section.FindContext<UMediaProfileMediaItemDisplayContext>();
				TSharedPtr<SMediaProfileMediaItemDisplayBase<TMediaItem>> ContextWidget = Context->GetDisplayWidget<SMediaProfileMediaItemDisplayBase<TMediaItem>>();
				
				if (!ContextWidget.IsValid())
				{
					return;
				}
				
				Section.AddEntry(ContextWidget->CreateMediaItemsComboButton());
				ContextWidget->AddToolbarEntries(Section);
				Section.AddEntry(ContextWidget->CreateViewportArrangementMenu());

				//TODO: Hidden for now until per-source timecode/genlock can be figured out (see UE-UE-305891)
				//Section.AddEntry(ContextWidget->CreateSettingsMenu());
			}));
		}
		
		FToolMenuContext Context;
		{
			Context.AppendCommandList(CommandList);
			AppendToolbarCommandList(Context);
			
			UMediaProfileMediaItemDisplayContext* ContextObject = NewObject<UMediaProfileMediaItemDisplayContext>();
			ContextObject->SetDisplayWidget<SMediaProfileMediaItemDisplayBase<TMediaItem>>(SharedThis(this));
			Context.AddObject(ContextObject);
		}

		return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, Context);
	}

	/** Creates the combo button used to change which media item is being displayed in the current panel */
	FToolMenuEntry CreateMediaItemsComboButton()
	{
		return FToolMenuEntry::InitSubMenu(
			TEXT("ActiveMediaItem"),
			TAttribute<FText>::CreateSP(this, &SMediaProfileMediaItemDisplayBase::GetActiveMediaItemLabel),
			TAttribute<FText>(),
			FNewToolMenuDelegate::CreateSP(this, &SMediaProfileMediaItemDisplayBase::GetMediaItemsDropdownContent),
			false,
			TAttribute<FSlateIcon>::CreateSP(this, &SMediaProfileMediaItemDisplayBase::GetActiveMediaItemIcon));	
	}

	/** Creates the dropdown list for the media items combo box, which lists all possible media items that can be displayed in the panel */
	void GetMediaItemsDropdownContent(UToolMenu* ToolMenu)
	{
		UMediaProfile* MediaProfile = GetMediaProfile();

		FToolMenuSection& MediaSourcesSection = ToolMenu->AddSection(TEXT("MediaSourcesSection"), LOCTEXT("MediaSourcesSectionLabel", "Media Sources"));
		for (int32 Index = 0; Index < MediaProfile->NumMediaSources(); ++Index)
		{
			UMediaSource* MediaSource = MediaProfile->GetMediaSource(Index);
			UClass* Class = MediaSource ? MediaSource->GetClass() : UMediaSource::StaticClass();
				
			const FText MediaSourceLabel = MediaSource ? FText::FromString(MediaProfile->GetLabelForMediaSource(Index)) : LOCTEXT("NoMediaSourceLabel", "No Media Source Set");

			MediaSourcesSection.AddMenuEntry(
				FName(FString::Format(TEXT("MediaSourceEntry_{0}"), { Index })),
				FText::Format(LOCTEXT("MediaItemMenuEntryFormat", "{0}: {1}"), Index + 1, MediaSourceLabel),
				TAttribute<FText>(),
				MediaSource ? FSlateIconFinder::FindIconForClass(Class) : FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SMediaProfileMediaItemDisplayBase<TMediaItem>::ChangeActiveMediaItem, Class, Index),
					FCanExecuteAction::CreateLambda([bIsMediaSourceSet = MediaSource != nullptr]()
					{
						return bIsMediaSourceSet;
					})));
		}

		FToolMenuSection& MediaOutputsSection = ToolMenu->AddSection(TEXT("MediaOutputsSection"), LOCTEXT("MediaOutputsSectionLabel", "Media Outputs"));
		for (int32 Index = 0; Index < MediaProfile->NumMediaSources(); ++Index)
		{
			UMediaOutput* MediaOutput = MediaProfile->GetMediaOutput(Index);
			UClass* Class = MediaOutput ? MediaOutput->GetClass() : UMediaOutput::StaticClass(); 

			const FText MediaOutputLabel = MediaOutput ? FText::FromString(MediaProfile->GetLabelForMediaOutput(Index)) : LOCTEXT("NoMediaOutputLabel", "No Media Output Set");
			MediaOutputsSection.AddMenuEntry(
				FName(FString::Format(TEXT("MediaOutputEntry_{0}"), { Index })),
				FText::Format(LOCTEXT("MediaItemMenuEntryFormat", "{0}: {1}"), Index + 1, MediaOutputLabel),
				TAttribute<FText>(),
				MediaOutput ? FSlateIconFinder::FindIconForClass(Class) : FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SMediaProfileMediaItemDisplayBase<TMediaItem>::ChangeActiveMediaItem, Class, Index),
					FCanExecuteAction::CreateLambda([bIsMediaOutputSet = MediaOutput != nullptr]()
					{
						return bIsMediaOutputSet;
					}))
				);
		}

		FToolMenuSection& LockPanelSection = ToolMenu->AddSection(TEXT("LockPanelSection"));
		LockPanelSection.AddSeparator(NAME_None);
		
		FToolMenuEntry& LockButton = LockPanelSection.AddMenuEntry(
			TEXT("LockDisplayButton"),
			LOCTEXT("LockDisplayLabel", "Lock Display"),
			LOCTEXT("LockDisplayTooltip", "Locks this display to the current media item"),
			TAttribute<FSlateIcon>::CreateSP(this, &SMediaProfileMediaItemDisplayBase::GetLockIcon),
			FUIAction(
				FExecuteAction::CreateSP(this, &SMediaProfileMediaItemDisplayBase::OnPanelLockToggled),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SMediaProfileMediaItemDisplayBase::IsPanelLocked)),
			EUserInterfaceActionType::ToggleButton);
			
		LockButton.SetShowInToolbarTopLevel(true);
		LockButton.ToolBarData.ResizeParams.AllowClipping = false;
	}

	/** Creates the viewport arrangement menu, which allows users to change the arrangement of panels in the viewport */
	FToolMenuEntry CreateViewportArrangementMenu()
	{
		FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
			"ViewportArrangementMenu",
			LOCTEXT("ViewportDropdownLabel", "..."),
			LOCTEXT("ViewportArrangementTooltip", "Viewport arrangements"),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* Submenu)
			{
				TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
				if (!PinnedOwningViewport)
				{
					return;
				}
				
				auto AddLayoutSectionFn = [Submenu, CommandList = PinnedOwningViewport->GetCommandList()](const FName& SectionName, const FText& SectionLabel, const TArray<TSharedPtr<FUICommandInfo>>& Commands)
				{
					FToolMenuSection& Section = Submenu->AddSection(SectionName, SectionLabel);

					FSlimHorizontalToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization::None);
					ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
					ToolbarBuilder.SetStyle(&FAppStyle::Get(), "ViewportLayoutToolbar");

					for (const TSharedPtr<FUICommandInfo>& Command : Commands)
					{
						ToolbarBuilder.AddToolBarButton(Command);
					}

					TSharedRef<SWidget> Widget = SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							ToolbarBuilder.MakeWidget()
						]
						
						+SHorizontalBox::Slot()
						.FillWidth(1)
						[
							SNullWidget::NullWidget
						];
					
					const FText Label = FText::GetEmpty();
					constexpr bool bNoIndent = true;
					Section.AddEntry(FToolMenuEntry::InitWidget(SectionName, Widget, Label, bNoIndent));
				};

				FLevelViewportCommands& Commands = FLevelViewportCommands::Get();
				
				AddLayoutSectionFn(TEXT("ViewportOnePaneConfigs"), LOCTEXT("OnePaneConfigHeader", "One Pane"), { Commands.ViewportConfig_OnePane });
				AddLayoutSectionFn(TEXT("ViewportTwoPaneConfigs"), LOCTEXT("TwoPaneConfigHeader", "Two Panes"), { Commands.ViewportConfig_TwoPanesH, Commands.ViewportConfig_TwoPanesV });
				AddLayoutSectionFn(TEXT("ViewportThreePaneConfigs"), LOCTEXT("ThreePaneConfigHeader", "Three Panes"), {
					Commands.ViewportConfig_ThreePanesLeft,
					Commands.ViewportConfig_ThreePanesRight,
					Commands.ViewportConfig_ThreePanesTop,
					Commands.ViewportConfig_ThreePanesBottom});
				AddLayoutSectionFn(TEXT("ViewportFourPaneConfigs"), LOCTEXT("FourPaneConfigHeader", "Four Panes"), {
					Commands.ViewportConfig_FourPanes2x2,
					Commands.ViewportConfig_FourPanesLeft,
					Commands.ViewportConfig_FourPanesRight,
					Commands.ViewportConfig_FourPanesTop,
					Commands.ViewportConfig_FourPanesBottom});

				
				FToolMenuSection& MaximizeSection = Submenu->FindOrAddSection("MaximizeSection");
			
				MaximizeSection.AddSeparator("MaximizeSeparator");
				MaximizeSection.AddEntry(FToolMenuEntry::InitMenuEntry(FLevelViewportCommands::Get().ToggleImmersive));
				MaximizeSection.AddEntry(CreateMaximizeViewportButton());
			}));

		Entry.StyleNameOverride = FName("ViewportToolbarViewportSizingSubmenu");
		Entry.InsertPosition.Position = EToolMenuInsertType::Last;
		Entry.ToolBarData.LabelOverride = FText();
		Entry.ToolBarData.ResizeParams.AllowClipping = false;

		return Entry;
	}

	/** Creates the menu entry to maximize this panel */
	FToolMenuEntry CreateMaximizeViewportButton()
	{
		FToolMenuEntry MaximizeRestoreEntry = FToolMenuEntry::InitMenuEntry(FLevelViewportCommands::Get().ToggleMaximize,
			TAttribute<FText>::CreateLambda([this]()
			{
				return IsMaximized() ?
					LOCTEXT("MaximizeRestoreLabel_Restore", "Restore All Viewports") :
					LOCTEXT("MaximizeRestoreLabel_Maximize", "Maximize Viewport");
			}),
			TAttribute<FText>::CreateLambda([this]()
			{
				return IsMaximized() ?
					LOCTEXT("MaximizeRestoreTooltip_Restore", "Restores the layout to show all viewports") :
					LOCTEXT("MaximizeRestoreTooltip_Maximize", "Maximizes this viewport");
			}),
			TAttribute<FSlateIcon>::CreateLambda([this]()
			{
				return IsMaximized() ?
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewportToolBar.Maximize.Checked") :
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewportToolBar.Maximize.Normal");
			})
		);
				
		MaximizeRestoreEntry.SetShowInToolbarTopLevel(true);
		MaximizeRestoreEntry.ToolBarData.ResizeParams.AllowClipping = false;
		MaximizeRestoreEntry.StyleNameOverride = FName("ViewportToolbarViewportSizingSubmenu");

		return MaximizeRestoreEntry;
	}

	FToolMenuEntry CreateSettingsMenu()
	{
		FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
			"SettingsMenu",
			TAttribute<FText>(),
			TAttribute<FText>(),
			FNewToolMenuDelegate::CreateLambda([this](UToolMenu* Submenu)
			{
				FToolMenuSection& TimecodeGenlockSection = Submenu->AddSection("TimecodeGenlockSettingsSection", LOCTEXT("TimecodeGenlockSettingsSectionLabel", "Timecode & Genlock"));
				TimecodeGenlockSection.AddMenuEntry(
					"ShowTimecodeInToolbar",
					LOCTEXT("ShowTimecodeInToolbarLabel", "Show Timecode in Viewport Toolbar"),
					LOCTEXT("ShowTimecodeInToolbarTooltip", "Whether to show the timecode in the viewport toolbar"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([]
						{
							if (UMediaProfileEditorUserSettings* UserSettings = GetMutableDefault<UMediaProfileEditorUserSettings>())
							{
								UserSettings->bShowTimecodeInViewportToolbar = !UserSettings->bShowTimecodeInViewportToolbar;
								UserSettings->SaveConfig();
							}
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([]
						{
							if (const UMediaProfileEditorUserSettings* UserSettings = GetDefault<UMediaProfileEditorUserSettings>())
							{
								return UserSettings->bShowTimecodeInViewportToolbar;
							}

							return true;
						})),
						EUserInterfaceActionType::ToggleButton);
				
				TimecodeGenlockSection.AddMenuEntry(
					"ShowGenlockInToolbar",
					LOCTEXT("ShowGenlockInToolbarLabel", "Show Genlock in Viewport Toolbar"),
					LOCTEXT("ShowGenlockInToolbarTooltip", "Whether to show the genlock status in the viewport toolbar"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([]
						{
							if (UMediaProfileEditorUserSettings* UserSettings = GetMutableDefault<UMediaProfileEditorUserSettings>())
							{
								UserSettings->bShowGenlockInViewportToolbar = !UserSettings->bShowGenlockInViewportToolbar;
								UserSettings->SaveConfig();
							}
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([]
						{
							if (const UMediaProfileEditorUserSettings* UserSettings = GetDefault<UMediaProfileEditorUserSettings>())
							{
								return UserSettings->bShowGenlockInViewportToolbar;
							}

							return true;
						})),
						EUserInterfaceActionType::ToggleButton);
			}),
			false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings")
		);

		return Entry;
	}
	
	/** Gets the label to display for this panel's content in the media items combo button */
	virtual FText GetActiveMediaItemLabel() const
	{
		if (GetMediaItem())
		{
			return FText::Format(LOCTEXT("ActiveMediaItemLabelFormat", "{0} {1}: {2}"), GetBaseMediaTypeLabel(), MediaItemIndex + 1, GetMediaItemLabel());
		}

		return FText::Format(LOCTEXT("ActiveMediaItemLabelFormat", "{0} {1}: {2}"), GetBaseMediaTypeLabel(), MediaItemIndex + 1, LOCTEXT("MediaNotConfiguredLabel", "Media not configured"));
	}

	/** Gets the icon to display for this panel's content in the media items combo button */
	virtual FSlateIcon GetActiveMediaItemIcon() const
	{
		if (TMediaItem* MediaItem = GetMediaItem())
		{
			return FSlateIconFinder::FindIconForClass(MediaItem->GetClass());
		}

		return FSlateIconFinder::FindIconForClass(TMediaItem::StaticClass());
	}

	/** Raised when the user has selected a new media item in the media items combo button */
	virtual void ChangeActiveMediaItem(UClass* InMediaItemClass, int32 InMediaItemIndex)
	{
		bool bRefreshDisplay = true;
		if (InMediaItemClass->IsChildOf(TMediaItem::StaticClass()))
		{
			// If the media item is the same type as this widget's type, we can update the index and refresh the widget
			MediaItemIndex = InMediaItemIndex;
			ConfigureMediaImage();
			bRefreshDisplay = false;
		}

		ChangePanelContents(InMediaItemClass, InMediaItemIndex, bRefreshDisplay);
	}

	/** Gets whether this panel is currently maximized in the viewport */
	bool IsMaximized() const
	{
		TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
		if (!PinnedOwningViewport.IsValid())
		{
			return false;
		}

		return PinnedOwningViewport->IsPanelMaximized(PanelIndex);
	}

	/** Gets whether this panel is currently immersive in the viewport */
	bool IsImmersive() const
	{
		TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
		if (!PinnedOwningViewport.IsValid())
		{
			return false;
		}

		return PinnedOwningViewport->IsPanelImmersive(PanelIndex);
	}

	/** Changes the panel's contents, recreating the display widgets */
	void ChangePanelContents(UClass* InMediaItemClass, int32 InMediaItemIndex, bool bRefreshDisplay)
	{
		TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
		if (!PinnedOwningViewport.IsValid())
		{
			return;
		}

		PinnedOwningViewport->SetPanelContents(PanelIndex, InMediaItemClass, InMediaItemIndex, bRefreshDisplay);
	}

	/** Raised when the lock button is toggled */
	void OnPanelLockToggled()
	{
		TSharedPtr<SMediaProfileViewport> PinnedOwningViewport = OwningViewport.Pin();
		if (!PinnedOwningViewport.IsValid())
		{
			return;
		}

		PinnedOwningViewport->SetPanelLocked(PanelIndex, !IsPanelLocked());
	}

	/** Gets whether this panel is locked or not */
	bool IsPanelLocked() const
	{
		return OwningViewport.IsValid() && OwningViewport.Pin()->IsPanelLocked(PanelIndex);
	}

	/** Gets the lock button icon */
	FSlateIcon GetLockIcon() const
	{
		return IsPanelLocked() ?
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("PropertyWindow.Locked")) :
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("PropertyWindow.Unlocked"));
	}

	/** Raised when an object property is changed in the editor, used to potentially refresh the display if the displayed media item is modified */
	virtual void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
	{
		if (!InObject)
		{
			return;
		}
		
		if (InObject == GetMediaItem())
		{
			ConfigureMediaImage();
		}
	}
	
protected:
	/** The media profile editor that owns this display */
	TWeakPtr<FMediaProfileEditor> MediaProfileEditor;

	/** The viewport widget that owns this display */
	TWeakPtr<SMediaProfileViewport> OwningViewport;

	/** The index of the panel that is displaying this display */
	int32 PanelIndex = INDEX_NONE;

	/** The index of the media item being displayed by this display */
	int32 MediaItemIndex = INDEX_NONE;

	/** Entry for the media profile's timecode and genlock configuration to display in the viewport toolbar */
	TSharedPtr<FMediaFrameworkTimecodeGenlockToolMenuEntry> TimecodeGenlockToolMenuEntry;
	
	TSharedPtr<SOverlay> Overlay;
	TSharedPtr<SBorder> MediaImageContainer;
	TSharedPtr<FUICommandList> CommandList;
};

#undef LOCTEXT_NAMESPACE

/** Implementation of SMediaProfileMediaItemDisplayBase for media sources, which displays the live feed from the selected media source */
class SMediaProfileMediaSourceDisplay : public SMediaProfileMediaItemDisplayBase<UMediaSource>
{
public:
	SLATE_BEGIN_ARGS(SMediaProfileMediaSourceDisplay) {}
		SLATE_ARGUMENT(TWeakPtr<FMediaProfileEditor>, MediaProfileEditor)
		SLATE_ARGUMENT(int32, PanelIndex)
		SLATE_ARGUMENT(int32, MediaItemIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<SMediaProfileViewport>& InOwningViewport);

protected:
	virtual void ConfigureMediaImage() override;
	
	virtual UMediaSource* GetMediaItem() const override;
	virtual FText GetMediaItemLabel() const override;
	virtual FText GetBaseMediaTypeLabel() const override;

private:
	UMediaTexture* GetMediaTexture() const;
	FVector2D GetSourceImageSize() const;
	FOptionalSize GetSourceAspectRatio() const;
	
	FText GetTimeDurationText() const;
	FText GetFramerateText() const;
};

/** Implementation of SMediaProfileMediaItemDisplayBase for media outputs, which displays the result from a media capture from the selected media output */
class SMediaProfileMediaOutputDisplay : public SMediaProfileMediaItemDisplayBase<UMediaOutput>
{
public:
	SLATE_BEGIN_ARGS(SMediaProfileMediaOutputDisplay) {}
		SLATE_ARGUMENT(TWeakPtr<FMediaProfileEditor>, MediaProfileEditor)
		SLATE_ARGUMENT(int32, PanelIndex)
		SLATE_ARGUMENT(int32, MediaItemIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<SMediaProfileViewport>& InOwningViewport);

	virtual ~SMediaProfileMediaOutputDisplay() override;
	
protected:
	virtual void ConfigureMediaImage() override;
	
	virtual UMediaOutput* GetMediaItem() const override;
	virtual FText GetMediaItemLabel() const override;
	virtual FText GetBaseMediaTypeLabel() const override;

	virtual void AddToolbarEntries(FToolMenuSection& Section) override;
	virtual void AppendToolbarCommandList(FToolMenuContext& Context) override;
	
	virtual void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent) override;

private:
	TOptional<EMediaCaptureState> GetMediaOutputCaptureState() const;
	FOptionalSize GetMediaOutputDesiredAspectRatio() const;
	
	void OnCaptureMethodChanged(UMediaOutput* MediaOutput);
	
private:
	TSharedPtr<SWidget> ImageWidget;
	TSharedPtr<FUICommandList> ShowFlagsCommandList;
	int32 ViewportOutputInfoIndex = INDEX_NONE;
	int32 RenderTargetOutputInfoIndex = INDEX_NONE;
	bool bDisplayShowFlags = false;
};

/** Dummy display for when a panel needs to be displayed that doesn't contain any media item */
class SMediaProfileDummyDisplay : public SMediaProfileMediaItemDisplayBase<UObject>
{
public:
	SLATE_BEGIN_ARGS(SMediaProfileDummyDisplay) {}
		SLATE_ARGUMENT(TWeakPtr<FMediaProfileEditor>, MediaProfileEditor)
		SLATE_ARGUMENT(int32, PanelIndex)
	SLATE_END_ARGS()
		
	void Construct(const FArguments& InArgs, const TSharedPtr<SMediaProfileViewport>& InOwningViewport);

protected:
	virtual void ConfigureMediaImage() override;
	
	virtual UObject* GetMediaItem() const override { return nullptr; }
	virtual FText GetMediaItemLabel() const override { return FText::GetEmpty(); }
	virtual FText GetBaseMediaTypeLabel() const override { return FText::GetEmpty(); }

	virtual FText GetActiveMediaItemLabel() const override;
	virtual FSlateIcon GetActiveMediaItemIcon() const override { return FSlateIcon(); }
	virtual void ChangeActiveMediaItem(UClass* InMediaItemClass, int32 InMediaItemIndex) override;
};