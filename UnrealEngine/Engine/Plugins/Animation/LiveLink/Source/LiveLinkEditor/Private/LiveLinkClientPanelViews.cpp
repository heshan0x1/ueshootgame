// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkClientPanelViews.h"

#include "Algo/Accumulate.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "Features/IModularFeatures.h"
#include "Framework/Commands/UICommandList.h"
#include "IDetailsView.h"
#include "ILiveLinkClient.h"
#include "ILiveLinkModule.h"
#include "Internationalization/Text.h"
#include "LiveLinkClient.h"
#include "LiveLinkClientCommands.h"
#include "LiveLinkSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkTypes.h"
#include "LiveLinkVirtualSubject.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SLiveLinkDataView.h"
#include "SPositiveActionButton.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "Widgets/Layout/SUniformGridPanel.h"


#define LOCTEXT_NAMESPACE "LiveLinkClientPanel.PanelViews"

// Static Source UI FNames
namespace SourceListUI
{
	static const FName TypeColumnName(TEXT("Type"));
	static const FName MachineColumnName(TEXT("Machine"));
	static const FName StatusColumnName(TEXT("Status"));
	static const FName ActionsColumnName(TEXT("Action"));
};

// Static Subject UI FNames
namespace SubjectTreeUI
{
	static const FName EnabledColumnName(TEXT("Enabled"));
	static const FName NameColumnName(TEXT("Name"));
	static const FName RoleColumnName(TEXT("Role"));
	static const FName TranslatedRoleColumnName(TEXT("TranslatedRole"));
	static const FName ActionsColumnName(TEXT("Action"));
};

namespace UE::LiveLink
{
TSharedPtr<IDetailsView> CreateSourcesDetailsView(const TSharedPtr<FLiveLinkSourcesView>& InSourcesView, const TAttribute<bool>& bInReadOnly)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.ViewIdentifier = NAME_None;
	TSharedPtr<IDetailsView> SettingsDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	// todo: use controller here instead of view widget
	SettingsDetailsView->OnFinishedChangingProperties().AddRaw(InSourcesView.Get(), &FLiveLinkSourcesView::OnPropertyChanged);
	SettingsDetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda(
		[bInReadOnly](){
		return !bInReadOnly.Get();
	}));

	return SettingsDetailsView;
}

TSharedPtr<SLiveLinkDataView> CreateSubjectsDetailsView(FLiveLinkClient* InLiveLinkClient, const TAttribute<bool>& bInReadOnly)
{
	return SNew(SLiveLinkDataView, InLiveLinkClient)
		.ReadOnly(bInReadOnly);
}
} // namespace UE::LiveLink


/** Dialog to create a new virtual subject */
class SVirtualSubjectCreateDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVirtualSubjectCreateDialog) {}

		/** Pointer to the LiveLinkClient instance. */
		SLATE_ARGUMENT(FLiveLinkClient*, LiveLinkClient)

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs)
	{
		static const FName DefaultVirtualSubjectName = TEXT("Virtual");

		bOkClicked = false;
		VirtualSubjectClass = nullptr;
		LiveLinkClient = InArgs._LiveLinkClient;

		check(LiveLinkClient);

		int32 NumVirtualSubjects = Algo::TransformAccumulate(LiveLinkClient->GetSubjects(true, true), [this](const FLiveLinkSubjectKey& SubjectKey)
			{
				return LiveLinkClient->IsVirtualSubject(SubjectKey) ? 1 : 0;
			}, 0);

		VirtualSubjectName = DefaultVirtualSubjectName;

		if (NumVirtualSubjects > 0)
		{
			VirtualSubjectName = *FString::Printf(TEXT("%s %d"), *DefaultVirtualSubjectName.ToString(), NumVirtualSubjects + 1);
		}

		//Default VirtualSubject Source should always exist
		TArray<FGuid> Sources = LiveLinkClient->GetVirtualSources();
		check(Sources.Num() > 0);
		VirtualSourceGuid = Sources[0];

		TSharedPtr<STextEntryPopup> TextEntry;
		SAssignNew(TextEntry, STextEntryPopup)
			.Label(LOCTEXT("AddVirtualSubjectName", "New Virtual Subject Name"))
			.DefaultText(FText::FromName(VirtualSubjectName))
			.OnTextChanged(this, &SVirtualSubjectCreateDialog::HandleAddVirtualSubjectChanged);

		VirtualSubjectTextWidget = TextEntry;

		ChildSlot
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("Menu.Background"))
					[
						SNew(SBox)
							[
								SNew(SVerticalBox)
								+SVerticalBox::Slot()
								.HAlign(HAlign_Fill)
								.AutoHeight()
								[
									TextEntry->AsShared()
								]

								+ SVerticalBox::Slot()
								.FillHeight(1.0)
								[
									SNew(SBorder)
										.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
										.Content()
										[
											SAssignNew(RoleClassPicker, SVerticalBox)
										]
								]

								// Ok/Cancel buttons
								+ SVerticalBox::Slot()
								.AutoHeight()
								.HAlign(HAlign_Right)
								.VAlign(VAlign_Bottom)
								.Padding(8)
								[
									SNew(SUniformGridPanel)
										.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
										.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
										.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
										+ SUniformGridPanel::Slot(0, 0)
										[
											SNew(SButton)
												.HAlign(HAlign_Center)
												.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
												.OnClicked(this, &SVirtualSubjectCreateDialog::OkClicked)
												.Text(LOCTEXT("AddVirtualSubjectAdd", "Add"))
												.IsEnabled(this, &SVirtualSubjectCreateDialog::IsVirtualSubjectClassSelected)
										]
										+ SUniformGridPanel::Slot(1, 0)
										[
											SNew(SButton)
												.HAlign(HAlign_Center)
												.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
												.OnClicked(this, &SVirtualSubjectCreateDialog::CancelClicked)
												.Text(LOCTEXT("AddVirtualSubjectCancel", "Cancel"))
										]
								]
							]
					]
			];

		MakeRoleClassPicker();
	}

	bool IsVirtualSubjectClassSelected() const
	{
		return VirtualSubjectClass != nullptr;
	}

	bool ConfigureVirtualSubject()
	{
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("CreateVirtualSubjectCreation", "Create Virtual Subject"))
			.ClientSize(FVector2D(400, 300))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			[
				AsShared()
			];

		PickerWindow = Window;

		GEditor->EditorAddModalWindow(Window);

		return bOkClicked;
	}

private:

	/** Class filter to display virtual subjects classes for the role class picker.. */
	class FLiveLinkRoleClassFilter : public IClassViewerFilter
	{
	public:
		FLiveLinkRoleClassFilter() = default;

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			if (InClass->IsChildOf(ULiveLinkVirtualSubject::StaticClass()))
			{
				return InClass->GetDefaultObject<ULiveLinkVirtualSubject>()->GetRole() != nullptr && !InClass->HasAnyClassFlags(CLASS_Abstract | CLASS_HideDropDown | CLASS_Deprecated);
			}
			return false;
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(ULiveLinkVirtualSubject::StaticClass());
		}
	};

	/** Creates the combo menu for the role class */
	void MakeRoleClassPicker()
	{
		// Load the classviewer module to display a class picker
		FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

		// Fill in options
		FClassViewerInitializationOptions Options;
		Options.Mode = EClassViewerMode::ClassPicker;

		Options.ClassFilters.Add(MakeShared<FLiveLinkRoleClassFilter>());

		RoleClassPicker->ClearChildren();
		RoleClassPicker->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(LOCTEXT("VirtualSubjectRole", "Virtual Subject Role:"))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
			];

		RoleClassPicker->AddSlot()
			[
				ClassViewerModule.CreateClassViewer(Options, FOnClassPicked::CreateSP(this, &SVirtualSubjectCreateDialog::OnClassPicked))
			];
	}

	/** Handler for when a parent class is selected */
	void OnClassPicked(UClass* ChosenClass)
	{
		VirtualSubjectClass = ChosenClass;
	}

	/** Handler for when ok is clicked */
	FReply OkClicked()
	{
		if (LiveLinkClient)
		{
			const FLiveLinkSubjectKey NewVirtualSubjectKey(VirtualSourceGuid, VirtualSubjectName);
			LiveLinkClient->AddVirtualSubject(NewVirtualSubjectKey, VirtualSubjectClass);
		}

		CloseDialog(true);

		return FReply::Handled();
	}

	/** Close the role picker window. */
	void CloseDialog(bool bWasPicked = false)
	{
		bOkClicked = bWasPicked;
		if (PickerWindow.IsValid())
		{
			PickerWindow.Pin()->RequestDestroyWindow();
		}
	}

	/** Handler for when cancel is clicked */
	FReply CancelClicked()
	{
		CloseDialog();
		return FReply::Handled();
	}

	/** Handles closing the role picker window when pressing escape. */
	FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			CloseDialog();
			return FReply::Handled();
		}
		return SWidget::OnKeyDown(MyGeometry, InKeyEvent);
	}

	/** Handles setting the error when changing the name of a new virtual subject. */
	void HandleAddVirtualSubjectChanged(const FText& NewSubjectName)
	{
		TSharedPtr<STextEntryPopup> VirtualSubjectTextWidgetPin = VirtualSubjectTextWidget.Pin();
		if (VirtualSubjectTextWidgetPin.IsValid())
		{
			TArray<FLiveLinkSubjectKey> SubjectKey = LiveLinkClient->GetSubjects(true, true);
			FName SubjectName = *NewSubjectName.ToString();
			const FLiveLinkSubjectKey ThisSubjectKey(VirtualSourceGuid, SubjectName);

			if (SubjectName.IsNone())
			{
				VirtualSubjectTextWidgetPin->SetError(LOCTEXT("VirtualInvalidName", "Invalid Virtual Subject"));
			}
			else if (SubjectKey.FindByPredicate([ThisSubjectKey](const FLiveLinkSubjectKey& Key) { return Key == ThisSubjectKey; }))
			{
				VirtualSubjectTextWidgetPin->SetError(LOCTEXT("VirtualExistingName", "Subject already exist"));
			}
			else
			{
				VirtualSubjectName = SubjectName;
				VirtualSubjectTextWidgetPin->SetError(FText::GetEmpty());
			}
		}
	}

private:
	/** Cached livelink client. */
	FLiveLinkClient* LiveLinkClient;

	/** Holds the text entry widget to name a virtual subject. */
	TWeakPtr<STextEntryPopup> VirtualSubjectTextWidget;

	/** A pointer to the window that is asking the user to select a role class */
	TWeakPtr<SWindow> PickerWindow;

	/** The container for the role Class picker */
	TSharedPtr<SVerticalBox> RoleClassPicker;

	/** The virtual subject's class */
	TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass;

	/** Selected source guid */
	FGuid VirtualSourceGuid;

	/** The virtual subject's name */
	FName VirtualSubjectName;

	/** True if Ok was clicked */
	bool bOkClicked;
};

FGuid FLiveLinkSourceUIEntry::GetGuid() const
{
	return EntryGuid;
}
FText FLiveLinkSourceUIEntry::GetSourceType() const
{
	return Client->GetSourceType(EntryGuid);
}
FText FLiveLinkSourceUIEntry::GetMachineName() const
{
	return Client->GetSourceMachineName(EntryGuid);
}
FText FLiveLinkSourceUIEntry::GetStatus() const
{
	return Client->GetSourceStatus(EntryGuid);
}
ULiveLinkSourceSettings* FLiveLinkSourceUIEntry::GetSourceSettings() const
{
	return Client->GetSourceSettings(EntryGuid);
}
void FLiveLinkSourceUIEntry::RemoveFromClient() const
{
	Client->RemoveSource(EntryGuid);
}
FText FLiveLinkSourceUIEntry::GetDisplayName() const
{
	return GetSourceType();
}
FText FLiveLinkSourceUIEntry::GetToolTip() const
{
	return Client->GetSourceToolTip(EntryGuid);
}

FLiveLinkSubjectUIEntry::FLiveLinkSubjectUIEntry(const FLiveLinkSubjectKey& InSubjectKey, FLiveLinkClient* InClient, bool bInIsSource)
	: SubjectKey(InSubjectKey)
	, Client(InClient)
	, bIsSource(bInIsSource)
{
	if (InClient)
	{
		bIsVirtualSubject = InClient->IsVirtualSubject(InSubjectKey);
	}
}

bool FLiveLinkSubjectUIEntry::IsSubject() const
{
	return !bIsSource;
}

bool FLiveLinkSubjectUIEntry::IsSource() const
{
	return bIsSource;
}

bool FLiveLinkSubjectUIEntry::IsVirtualSubject() const
{
	return IsSubject() && bIsVirtualSubject;
}

UObject* FLiveLinkSubjectUIEntry::GetSettings() const
{
	if (IsSource())
	{
		return Client->GetSourceSettings(SubjectKey.Source);
	}
	else
	{
		return Client->GetSubjectSettings(SubjectKey);
	}
}

bool FLiveLinkSubjectUIEntry::IsSubjectEnabled() const
{
	return IsSubject() ? Client->IsSubjectEnabled(SubjectKey, false) : false;
}

bool FLiveLinkSubjectUIEntry::IsSubjectValid() const
{
	return IsSubject() ? Client->IsSubjectValid(SubjectKey) : false;
}

void FLiveLinkSubjectUIEntry::SetSubjectEnabled(bool bIsEnabled)
{
	if (IsSubject())
	{
		Client->SetSubjectEnabled(SubjectKey, bIsEnabled);
	}
}

FText FLiveLinkSubjectUIEntry::GetItemText() const
{
	if (IsSubject())
	{
		return Client->GetSubjectDisplayName(SubjectKey);
	}
	else
	{
		return Client->GetSourceNameOverride(SubjectKey);
	}
}

TSubclassOf<ULiveLinkRole> FLiveLinkSubjectUIEntry::GetItemRole() const
{
	return IsSubject() ? Client->GetSubjectRole_AnyThread(SubjectKey) : TSubclassOf<ULiveLinkRole>();
}

TSubclassOf<ULiveLinkRole> FLiveLinkSubjectUIEntry::GetItemTranslatedRole() const
{
	return IsSubject() ? Client->GetSubjectTranslatedRole_AnyThread(SubjectKey) : TSubclassOf<ULiveLinkRole>();
}

void FLiveLinkSubjectUIEntry::RemoveFromClient() const
{
	Client->RemoveSubject_AnyThread(SubjectKey);
}

void FLiveLinkSubjectUIEntry::PauseSubject()
{
	if (IsPaused())
	{
		Client->UnpauseSubject_AnyThread(SubjectKey.SubjectName);
	}
	else
	{
		Client->PauseSubject_AnyThread(SubjectKey.SubjectName);
	}
}

bool FLiveLinkSubjectUIEntry::IsPaused() const
{
	return Client->GetSubjectState(SubjectKey.SubjectName) == ELiveLinkSubjectState::Paused;
}

class SLiveLinkClientPanelSubjectRow : public SMultiColumnTableRow<FLiveLinkSubjectUIEntryPtr>
{
public:
	SLATE_BEGIN_ARGS(SLiveLinkClientPanelSubjectRow) {}
	/** The list item for this row */
	SLATE_ARGUMENT(FLiveLinkSubjectUIEntryPtr, Entry)
	SLATE_ATTRIBUTE(bool, ReadOnly)
	SLATE_END_ARGS()


	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		EntryPtr = Args._Entry;
		bReadOnly = Args._ReadOnly;

		TSharedPtr<FSlateStyleSet> StyleSet = ILiveLinkModule::Get().GetStyle();

		OkayIcon = StyleSet->GetBrush("LiveLink.Subject.Okay");
		WarningIcon = StyleSet->GetBrush("LiveLink.Subject.Warning");
		PauseIcon = StyleSet->GetBrush("LiveLink.Subject.Pause");

		SMultiColumnTableRow<FLiveLinkSubjectUIEntryPtr>::Construct(
			FSuperRowType::FArguments()
			.Style(&FAppStyle().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow"))
			.Padding(1.0f),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == SubjectTreeUI::EnabledColumnName)
		{
			if (EntryPtr->IsSubject())
			{
				return SNew(SCheckBox)
					.Visibility(this, &SLiveLinkClientPanelSubjectRow::GetVisibilityFromReadOnly)
					.IsChecked(MakeAttributeSP(this, &SLiveLinkClientPanelSubjectRow::GetSubjectEnabled))
					.OnCheckStateChanged(this, &SLiveLinkClientPanelSubjectRow::OnEnabledChanged);
			}
		}
		else if (ColumnName == SubjectTreeUI::NameColumnName)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6, 0, 0, 0)
				[
					SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(this, &SLiveLinkClientPanelSubjectRow::GetSubjectTextColor)
					.Text(MakeAttributeSP(this, &SLiveLinkClientPanelSubjectRow::GetItemText))
				];
		}
		else if (ColumnName == SubjectTreeUI::RoleColumnName)
		{
			TAttribute<FText> RoleAttribute = MakeAttributeSP(this, &SLiveLinkClientPanelSubjectRow::GetItemRole);

			return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.ColorAndOpacity(this, &SLiveLinkClientPanelSubjectRow::GetSubjectTextColor)
				.Text(EntryPtr->IsSubject() ? RoleAttribute : FText::GetEmpty())
			];
		}
		else if (ColumnName == SubjectTreeUI::TranslatedRoleColumnName)
		{
			TAttribute<FText> TranslatedRoleAttribute = MakeAttributeSP(this, &SLiveLinkClientPanelSubjectRow::GetItemTranslatedRole);

			return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
					.ColorAndOpacity(this, &SLiveLinkClientPanelSubjectRow::GetSubjectTextColor)
					.Text(EntryPtr->IsSubject() ? TranslatedRoleAttribute : FText::GetEmpty())
			];
		}
		else if (ColumnName == SubjectTreeUI::ActionsColumnName)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(this, &SLiveLinkClientPanelSubjectRow::GetSubjectIcon)
					.ToolTipText(this, &SLiveLinkClientPanelSubjectRow::GetSubjectIconToolTip)
				];
		}

		return SNullWidget::NullWidget;
	}

private:
	ECheckBoxState GetSubjectEnabled() const { return EntryPtr->IsSubjectEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }
	void OnEnabledChanged(ECheckBoxState NewState) { EntryPtr->SetSubjectEnabled(NewState == ECheckBoxState::Checked); }
	FText GetItemText() const { return EntryPtr->GetItemText(); }
	FText GetItemRole() const
	{
		TSubclassOf<ULiveLinkRole> Role = EntryPtr->GetItemRole();
		if (Role)
		{
			return Role->GetDefaultObject<ULiveLinkRole>()->GetDisplayName();
		}
		return FText::GetEmpty();
	}

	// If role is translated, put the original role in the extra info.
	FText GetItemTranslatedRole() const
	{
		TSubclassOf<ULiveLinkRole> TranslatedRole = EntryPtr->GetItemTranslatedRole();

		if (TranslatedRole.Get())
		{
			return TranslatedRole->GetDefaultObject<ULiveLinkRole>()->GetDisplayName();
		}
		return FText::GetEmpty();
	}

	/** Get widget visibility according to whether or not the panel is in read-only mode. */
	EVisibility GetVisibilityFromReadOnly() const
	{
		return bReadOnly.Get() ? EVisibility::Collapsed : EVisibility::Visible;
	}

	/** Get the icon for a subject's status. */
	const FSlateBrush* GetSubjectIcon() const
	{
		const FSlateBrush* IconBrush = nullptr;

		if (EntryPtr->IsSubjectEnabled())
		{
			if (!EntryPtr->IsPaused())
			{
				IconBrush = EntryPtr->IsSubjectValid() ? OkayIcon : WarningIcon;
			}
			else
			{
				IconBrush = PauseIcon;
			}
		}
		else
		{
			// No icon for disabled subjects, we rely on setting the text color to subdued foreground.
		}

		return IconBrush;
	}

	/** Get the tooltip for a subject's status icon. */
	FText GetSubjectIconToolTip() const
	{
		FText IconToolTip;

		if (EntryPtr->IsSubjectEnabled())
		{
			IconToolTip = EntryPtr->IsSubjectValid() ? LOCTEXT("ValidSubjectToolTip", "Subject is operating normally.") : LOCTEXT("InvalidSubjectToolTip", "Subject is invalid.");
		}
		else
		{
			IconToolTip = LOCTEXT("SubjectDisabledToolTip", "Subject is disabled.");
		}

		return IconToolTip;
	}

	/** Get the text color for a subject. */
	FSlateColor GetSubjectTextColor() const
	{
		FSlateColor TextColor = FSlateColor::UseForeground();

		if (EntryPtr->IsSubject() && !EntryPtr->IsSubjectEnabled())
		{
			TextColor = FSlateColor::UseSubduedForeground();
		}

		return TextColor;
	}

	FLiveLinkSubjectUIEntryPtr EntryPtr;

	/** Returns whether the panel is in read-only mode. */
	TAttribute<bool> bReadOnly;

	//~ Status icons
	const FSlateBrush* OkayIcon = nullptr;
	const FSlateBrush* WarningIcon = nullptr;
	const FSlateBrush* PauseIcon = nullptr;
};

class SLiveLinkClientPanelSourcesRow : public SMultiColumnTableRow<FLiveLinkSourceUIEntryPtr>
{
public:
	SLATE_BEGIN_ARGS(SLiveLinkClientPanelSourcesRow) {}
	/** The list item for this row */
		SLATE_ARGUMENT(FLiveLinkSourceUIEntryPtr, Entry)
		SLATE_ATTRIBUTE(bool, ReadOnly)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		EntryPtr = Args._Entry;
		bReadOnly = Args._ReadOnly;

		SMultiColumnTableRow<FLiveLinkSourceUIEntryPtr>::Construct(
			FSuperRowType::FArguments()
			.Style(&FAppStyle().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow"))
			.Padding(1.0f),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == SourceListUI::TypeColumnName)
		{
			return SNew(STextBlock)
				.Text(EntryPtr->GetSourceType());
		}
		else if (ColumnName == SourceListUI::MachineColumnName)
		{
			return SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SLiveLinkClientPanelSourcesRow::GetMachineName));
		}
		else if (ColumnName == SourceListUI::StatusColumnName)
		{
			return SNew(STextBlock)
				.Text(MakeAttributeSP(this, &SLiveLinkClientPanelSourcesRow::GetSourceStatus));
		}
		else if (ColumnName == SourceListUI::ActionsColumnName)
		{
			return SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility(this, &SLiveLinkClientPanelSourcesRow::GetVisibilityFromReadOnly)
				.OnClicked(this, &SLiveLinkClientPanelSourcesRow::OnRemoveClicked)
				.ToolTipText(LOCTEXT("RemoveSource", "Remove selected live link source"))
				.ContentPadding(0.f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				];
		}

		return SNullWidget::NullWidget;
	}

	FText GetToolTipText() const
	{
		return EntryPtr->GetToolTip();
	}

private:
	FText GetMachineName() const
	{
		return EntryPtr->GetMachineName();
	}

	FText GetSourceStatus() const
	{
		return EntryPtr->GetStatus();
	}

	FReply OnRemoveClicked()
	{
		EntryPtr->RemoveFromClient();
		return FReply::Handled();
	}

	/** Get widget visibility according to whether or not the panel is in read-only mode. */
	EVisibility GetVisibilityFromReadOnly() const
	{
		return bReadOnly.Get() ? EVisibility::Collapsed : EVisibility::Visible;
	}

private:
	FLiveLinkSourceUIEntryPtr EntryPtr;

	/** Attribute used to query whether the panel is in read only mode or not. */
	TAttribute<bool> bReadOnly;
};

FLiveLinkSourcesView::FLiveLinkSourcesView(FLiveLinkClient* InLiveLinkClient, TSharedPtr<FUICommandList> InCommandList, TAttribute<bool> bInReadOnly, FOnSourceSelectionChanged InOnSourceSelectionChanged)
	: Client(InLiveLinkClient)
	, OnSourceSelectionChangedDelegate(MoveTemp(InOnSourceSelectionChanged))
	, bReadOnly(MoveTemp(bInReadOnly))
{
	TArray<UClass*> Results;
	GetDerivedClasses(ULiveLinkSourceFactory::StaticClass(), Results, true);
	for (UClass* SourceFactory : Results)
	{
		if (!SourceFactory->HasAllClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) && CastChecked<ULiveLinkSourceFactory>(SourceFactory->GetDefaultObject())->IsEnabled())
		{
			Factories.Add(NewObject<ULiveLinkSourceFactory>(GetTransientPackage(), SourceFactory));
		}
	}

	Algo::StableSort(Factories, [](ULiveLinkSourceFactory* LHS, ULiveLinkSourceFactory* RHS)
	{
		return LHS->GetSourceDisplayName().CompareTo(RHS->GetSourceDisplayName()) <= 0;
	});

	CreateSourcesListView(InCommandList);
}

FLiveLinkSourcesView::~FLiveLinkSourcesView()
{
	FTSTicker::RemoveTicker(TickHandle);
}

TSharedRef<ITableRow> FLiveLinkSourcesView::MakeSourceListViewWidget(FLiveLinkSourceUIEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SLiveLinkClientPanelSourcesRow, OwnerTable)
		.Entry(Entry)
		.ReadOnly(bReadOnly)
		.ToolTipText_Lambda([Entry]() { return Entry->GetToolTip(); });
}

void FLiveLinkSourcesView::OnSourceListSelectionChanged(FLiveLinkSourceUIEntryPtr Entry, ESelectInfo::Type SelectionType) const
{
	OnSourceSelectionChangedDelegate.Execute(Entry, SelectionType);
}

void FLiveLinkSourcesView::CreateSourcesListView(const TSharedPtr<FUICommandList>& InCommandList)
{
	SAssignNew(HostWidget, SVerticalBox)
	+SVerticalBox::Slot()
	.Padding(FMargin(4.0, 4.0, 4.0, 6.0))
	.MinHeight(29)
	.AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(FMargin(4.0, 2.0))
		.FillWidth(1.0f)
		[
			SAssignNew(FilterSearchBox, SLiveLinkFilterSearchBox<FLiveLinkSourceUIEntryPtr>)
			.ItemSource(&SourceData)
			.OnGatherItems_Lambda([this](TArray<FLiveLinkSourceUIEntryPtr>& Items)
				{
					Items.Append(SourceData);
				})
			.OnUpdateFilteredList_Lambda([this](const TArray<FLiveLinkSourceUIEntryPtr>& FilteredItems)
				{
					FilteredList = FilteredItems;
					SourcesListView->RequestListRefresh();
				})
		]
		+ SHorizontalBox::Slot()
		.Padding(FMargin(4.0, 2.0))
		.AutoWidth()
		[
			SNew(SPositiveActionButton)
			.OnGetMenuContent_Raw(this, &FLiveLinkSourcesView::OnGenerateSourceMenu)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddSource", "Add Source"))
			.ToolTipText(LOCTEXT("AddSource_ToolTip", "Add a new Live Link source"))
		]
	]
	+SVerticalBox::Slot()
	.VAlign(VAlign_Fill)
	[
		SAssignNew(SourcesListView, SLiveLinkSourceListView, bReadOnly)
			.ListItemsSource(&FilteredList)
			.SelectionMode(ESelectionMode::Single)
			.ScrollbarVisibility(EVisibility::Visible)
			.OnGenerateRow_Raw(this, &FLiveLinkSourcesView::MakeSourceListViewWidget)
			.OnContextMenuOpening_Raw(this, &FLiveLinkSourcesView::OnSourceConstructContextMenu, InCommandList)
			.OnSelectionChanged_Raw(this, &FLiveLinkSourcesView::OnSourceListSelectionChanged)
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(SourceListUI::TypeColumnName)
				.FillWidth(25.f)
				.DefaultLabel(LOCTEXT("TypeColumnHeaderName", "Source Type"))
				+ SHeaderRow::Column(SourceListUI::MachineColumnName)
				.FillWidth(25.f)
				.DefaultLabel(LOCTEXT("MachineColumnHeaderName", "Source Machine"))
				+ SHeaderRow::Column(SourceListUI::StatusColumnName)
				.FillWidth(50.f)
				.DefaultLabel(LOCTEXT("StatusColumnHeaderName", "Status"))
				+ SHeaderRow::Column(SourceListUI::ActionsColumnName)
				.ManualWidth(20.f)
				.DefaultLabel(FText())
			)
	];
}

TSharedPtr<SWidget> FLiveLinkSourcesView::OnSourceConstructContextMenu(TSharedPtr<FUICommandList> InCommandList)
{
	if (bReadOnly.Get())
	{
		return nullptr;
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	MenuBuilder.BeginSection(TEXT("Remove"));
	{
		if (CanRemoveSource())
		{
			MenuBuilder.AddMenuEntry(FLiveLinkClientCommands::Get().RemoveSource);
		}
		MenuBuilder.AddMenuEntry(FLiveLinkClientCommands::Get().RemoveAllSources);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FLiveLinkSourcesView::GetWidget()
{
	return HostWidget.ToSharedRef();
}

void FLiveLinkSourcesView::RefreshSourceData(bool bRefreshUI)
{
	SourceData.Reset();
	FilteredList.Reset();

	for (FGuid SourceGuid : Client->GetDisplayableSources())
	{
		SourceData.Add(MakeShared<FLiveLinkSourceUIEntry>(SourceGuid, Client));
	}
	SourceData.Sort([](const FLiveLinkSourceUIEntryPtr& LHS, const FLiveLinkSourceUIEntryPtr& RHS) { return LHS->GetMachineName().CompareTo(RHS->GetMachineName()) < 0; });

	if (bRefreshUI && FilterSearchBox)
	{
		FilterSearchBox->Update();
	}
}

void FLiveLinkSourcesView::HandleRemoveSource()
{
	TArray<FLiveLinkSourceUIEntryPtr> Selected;
	SourcesListView->GetSelectedItems(Selected);

	if (Selected.Num() > 0)
	{
		Selected[0]->RemoveFromClient();
	}
}

bool FLiveLinkSourcesView::CanRemoveSource()
{
	return SourcesListView->GetNumItemsSelected() > 0;
}

FLiveLinkSubjectsView::FLiveLinkSubjectsView(FOnSubjectSelectionChanged InOnSubjectSelectionChanged, const TSharedPtr<FUICommandList>& InCommandList, TAttribute<bool> bInReadOnly)
	: SubjectSelectionChangedDelegate(InOnSubjectSelectionChanged)
	, bReadOnly(MoveTemp(bInReadOnly))
{
	CreateSubjectsTreeView(InCommandList);
}

void FLiveLinkSubjectsView::OnSubjectSelectionChanged(FLiveLinkSubjectUIEntryPtr SubjectEntry, ESelectInfo::Type SelectInfo)
{
	SubjectSelectionChangedDelegate.Execute(SubjectEntry, SelectInfo);
}

void FLiveLinkSourcesView::OnPropertyChanged(const FPropertyChangedEvent& InEvent)
{
	TArray<FLiveLinkSourceUIEntryPtr> Selected;
	SourcesListView->GetSelectedItems(Selected);
	for (FLiveLinkSourceUIEntryPtr Item : Selected)
	{
		Client->OnPropertyChanged(Item->GetGuid(), InEvent);
	}
}

TSharedRef<ITableRow> FLiveLinkSubjectsView::MakeTreeRowWidget(FLiveLinkSubjectUIEntryPtr InInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLiveLinkClientPanelSubjectRow, OwnerTable)
		.Entry(InInfo)
		.ReadOnly(bReadOnly);
}

void FLiveLinkSubjectsView::GetChildrenForInfo(FLiveLinkSubjectUIEntryPtr InInfo, TArray< FLiveLinkSubjectUIEntryPtr >& OutChildren)
{
	OutChildren = InInfo->Children;
}

TSharedPtr<SWidget> FLiveLinkSubjectsView::OnOpenVirtualSubjectContextMenu(TSharedPtr<FUICommandList> InCommandList)
{
	if (bReadOnly.Get())
	{
		return nullptr;
	}

	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	MenuBuilder.BeginSection(TEXT("Remove"));
	{
		if (CanRemoveSubject())
		{
			MenuBuilder.AddMenuEntry(FLiveLinkClientCommands::Get().RemoveSubject);
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddMenuEntry(FLiveLinkClientCommands::Get().PauseSubject, NAME_None, TAttribute<FText>::CreateSP(this, &FLiveLinkSubjectsView::GetPauseSubjectLabel), TAttribute<FText>::CreateSP(this, &FLiveLinkSubjectsView::GetPauseSubjectToolTip));

	return MenuBuilder.MakeWidget();
}

bool FLiveLinkSubjectsView::CanRemoveSubject() const
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsTreeView->GetSelectedItems(Selected);

	for (const FLiveLinkSubjectUIEntryPtr& Entry : Selected)
	{
		if (Entry && Entry->IsVirtualSubject())
		{
			return true;
		}
	}

	return false;
}

void FLiveLinkSubjectsView::RefreshSubjects()
{
	TArray<TPair<FLiveLinkSubjectKey, bool>> SavedSelection;
	{
		TArray<FLiveLinkSubjectUIEntryPtr> SelectedItems = SubjectsTreeView->GetSelectedItems();
		for (const FLiveLinkSubjectUIEntryPtr& SelectedItem : SelectedItems)
		{
			SavedSelection.Add(TPair<FLiveLinkSubjectKey, bool>(SelectedItem->SubjectKey, SelectedItem->IsSubject()));
		}
	}

	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		if (ILiveLinkClient* Client = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName))
		{
			TArray<FLiveLinkSubjectKey> SubjectKeys = Client->GetSubjects(true, true);
			SubjectData.Reset();

			TMap<FName, FLiveLinkSubjectUIEntryPtr> SourceItems;

			TArray<FLiveLinkSubjectUIEntryPtr> AllItems;
			AllItems.Reserve(SubjectKeys.Num());

			for (const FLiveLinkSubjectKey& SubjectKey : SubjectKeys)
			{
				FLiveLinkSubjectUIEntryPtr Source;

				FName SourceNameOverride = *Client->GetSourceNameOverride(SubjectKey).ToString();

				if (FLiveLinkSubjectUIEntryPtr* SourcePtr = SourceItems.Find(*SourceNameOverride.ToString()))
				{
					Source = *SourcePtr;
				}
				else
				{
					constexpr bool bIsSource = true;
					Source = MakeShared<FLiveLinkSubjectUIEntry>(SubjectKey, static_cast<FLiveLinkClient*>(Client), bIsSource);
					SubjectData.Add(Source);
					SourceItems.Add(SourceNameOverride) = Source;

					SubjectsTreeView->SetItemExpansion(Source, true);
					AllItems.Add(Source);
				}
				
				FLiveLinkSubjectUIEntryPtr SubjectEntry = MakeShared<FLiveLinkSubjectUIEntry>(SubjectKey, static_cast<FLiveLinkClient*>(Client));
				Source->Children.Add(SubjectEntry);
				AllItems.Add(SubjectEntry);
			}

			auto SortPredicate = [](const FLiveLinkSubjectUIEntryPtr& LHS, const FLiveLinkSubjectUIEntryPtr& RHS) {return LHS->GetItemText().CompareTo(RHS->GetItemText()) < 0; };
			SubjectData.Sort(SortPredicate);
			for (FLiveLinkSubjectUIEntryPtr& Subject : SubjectData)
			{
				Subject->Children.Sort(SortPredicate);
			}

			for (const FLiveLinkSubjectUIEntryPtr& Item : AllItems)
			{
				for (const TPair<FLiveLinkSubjectKey, bool>& Selection : SavedSelection)
				{
					if (Item->SubjectKey == Selection.Key && Item->IsSubject() == Selection.Value)
					{
						SubjectsTreeView->SetItemSelection(Item, true);
						break;
					}
				}
			}

			if (FilterSearchBox)
			{
				FilterSearchBox->Update();
			}
		}
	}
}

bool FLiveLinkSubjectsView::CanPauseSubject() const
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsTreeView->GetSelectedItems(Selected);

	for (const FLiveLinkSubjectUIEntryPtr& Entry : Selected)
	{
		if (Entry && Entry->IsSubjectValid() && Entry->IsSubjectEnabled())
		{
			return true;
		}
	}

	return false;
}

void FLiveLinkSubjectsView::HandlePauseSubject()
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsTreeView->GetSelectedItems(Selected);

	for (const FLiveLinkSubjectUIEntryPtr& Entry : Selected)
	{
		if (Entry && Entry->IsSubjectValid() && Entry->IsSubjectEnabled())
		{
			Entry->PauseSubject();
		}
	}
}

TSharedRef<SWidget> FLiveLinkSubjectsView::GetWidget()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(FMargin(4.0, 8.0, 4.0, 6.0))
		.MinHeight(29)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(FMargin(4.0, 2.0))
				.FillWidth(1.0f)
				[
					SAssignNew(FilterSearchBox, SLiveLinkFilterSearchBox<FLiveLinkSubjectUIEntryPtr>)
						.ItemSource(&SubjectData)
						.OnGatherItems_Lambda([this](TArray<FLiveLinkSubjectUIEntryPtr>& Items)
							{
								for (FLiveLinkSubjectUIEntryPtr Source : SubjectData)
								{
									for (FLiveLinkSubjectUIEntryPtr Subject : Source->Children)
									{
										Items.Add(Subject);
									}
								}
							})
						.OnUpdateFilteredList_Lambda([this](const TArray<FLiveLinkSubjectUIEntryPtr>& FilteredItems) 
							{
								FilteredList = FilteredItems;

								for (const FLiveLinkSubjectUIEntryPtr& Item : FilteredList)
								{
									SubjectsTreeView->SetItemExpansion(Item, true);
								}

								SubjectsTreeView->RequestListRefresh();
							})
				]
		]
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Fill)
		[
			SubjectsTreeView.ToSharedRef()
		];
}

void FLiveLinkSubjectsView::CreateSubjectsTreeView(const TSharedPtr<FUICommandList>& InCommandList)
{
	SAssignNew(SubjectsTreeView, SLiveLinkSubjectsTreeView, bReadOnly)
		.TreeItemsSource(&FilteredList)
		.OnGenerateRow_Raw(this, &FLiveLinkSubjectsView::MakeTreeRowWidget)
		.OnGetChildren_Raw(this, &FLiveLinkSubjectsView::GetChildrenForInfo)
		.OnSelectionChanged_Raw(this, &FLiveLinkSubjectsView::OnSubjectSelectionChanged)
		.OnContextMenuOpening_Raw(this, &FLiveLinkSubjectsView::OnOpenVirtualSubjectContextMenu, InCommandList)
		.SelectionMode(ESelectionMode::Multi)
		.HeaderRow
		(
			SNew(SHeaderRow)
			.CanSelectGeneratedColumn(true)
			.HiddenColumnsList({ SubjectTreeUI::TranslatedRoleColumnName })
			+ SHeaderRow::Column(SubjectTreeUI::EnabledColumnName)
			.DefaultTooltip(LOCTEXT("EnabledToolTip", "Whether this subject should be evaluated."))
			.DefaultLabel(FText())
			.FixedWidth(22)
			+ SHeaderRow::Column(SubjectTreeUI::NameColumnName)
			.DefaultLabel(LOCTEXT("SubjectItemName", "Subject Name"))
			.DefaultTooltip(LOCTEXT("NameToolTip", "Unique name for this subject."))
			.FillWidth(0.60f)
			+ SHeaderRow::Column(SubjectTreeUI::RoleColumnName)
			.DefaultLabel(LOCTEXT("RoleName", "Role"))
			.ManualWidth(90.f)
			.DefaultTooltip(LOCTEXT("RoleToolTip", "Role of this subject."))
			+ SHeaderRow::Column(SubjectTreeUI::TranslatedRoleColumnName)
			.DefaultLabel(LOCTEXT("TranslatedRoleName", "Translated"))
			.DefaultTooltip(LOCTEXT("TranslatedRoleToolTip", "Get the translated role of a subject if it's being translated before being rebroadcast. This should only be relevant in Live Link Hub."))
			.ManualWidth(70.f)
			+ SHeaderRow::Column(SubjectTreeUI::ActionsColumnName)
			.ManualWidth(20.f)
			.DefaultLabel(FText())
		);
}

FText FLiveLinkSubjectsView::GetPauseSubjectLabel() const
{
	FText Label = LOCTEXT("PauseSubjectLabel", "Pause Subject");

	if (IsSelectedSubjectPaused())
	{
		Label = LOCTEXT("UnpauseSubjectLabel", "Unpause Subject");
	}

	return Label;
}

FText FLiveLinkSubjectsView::GetPauseSubjectToolTip() const
{
	FText ToolTip = LOCTEXT("PauseSubjectToolTip", "Pause a subject, the last received data will be used for evaluation.");

	if (IsSelectedSubjectPaused())
	{
		ToolTip = LOCTEXT("UnpauseSubjectToolTip", "Unpause Subject and resume operating on live data.");
	}

	return ToolTip;
}

bool FLiveLinkSubjectsView::IsSelectedSubjectPaused() const
{
	TArray<FLiveLinkSubjectUIEntryPtr> Selected;
	SubjectsTreeView->GetSelectedItems(Selected);

	for (const FLiveLinkSubjectUIEntryPtr& EntryPtr : Selected)
	{
		if (EntryPtr && !EntryPtr->IsPaused())
		{
			return false;
		}
	}

	return true;
}

TSharedRef<SWidget> FLiveLinkSourcesView::OnGenerateSourceMenu()
{
	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, NULL);

	MenuBuilder.BeginSection("SourceSection", LOCTEXT("Sources", "Live Link Sources"));

	for (int32 FactoryIndex = 0; FactoryIndex < Factories.Num(); ++FactoryIndex)
	{
		ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex];
		if (FactoryInstance)
		{
			ULiveLinkSourceFactory::EMenuType MenuType = FactoryInstance->GetMenuType();

			if (MenuType == ULiveLinkSourceFactory::EMenuType::SubPanel)
			{
				MenuBuilder.AddMenuEntry(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FLiveLinkSourcesView::OpenCreateMenuWindow, FactoryIndex)
					),
					NAME_None,
					EUserInterfaceActionType::Button);
			}
			else if (MenuType == ULiveLinkSourceFactory::EMenuType::MenuEntry)
			{
				MenuBuilder.AddMenuEntry(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FLiveLinkSourcesView::ExecuteCreateSource, FactoryIndex)
					),
					NAME_None,
					EUserInterfaceActionType::Button);
			}
			else
			{
				MenuBuilder.AddMenuEntry(
					FactoryInstance->GetSourceDisplayName(),
					FactoryInstance->GetSourceTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction::CreateLambda([]() { return false; })
					),
					NAME_None,
					EUserInterfaceActionType::Button);
			}
		}
	}

	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("VirtualSourceSection", LOCTEXT("VirtualSources", "Live Link VirtualSubject Sources"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddVirtualSubject", "Add Virtual Subject"),
		LOCTEXT("AddVirtualSubject_Tooltip", "Adds a new virtual subject to Live Link. Instead of coming from a source a virtual subject is a combination of 2 or more real subjects"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FLiveLinkSourcesView::AddVirtualSubject)
		),
		NAME_None,
		EUserInterfaceActionType::Button);

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FLiveLinkSourcesView::OpenCreateMenuWindow(int32 FactoryIndex)
{
	if (Factories.IsValidIndex(FactoryIndex))
	{
		if (ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex])
		{
			TSharedPtr<SWidget> Widget = FactoryInstance->BuildCreationPanel(ULiveLinkSourceFactory::FOnLiveLinkSourceCreated::CreateSP(this, &FLiveLinkSourcesView::OnSourceCreated, TSubclassOf<ULiveLinkSourceFactory>(FactoryInstance->GetClass())));
			if (Widget.IsValid())
			{
				FText CreateText = FText::Format(LOCTEXT("CreateSourceLabel", "Create {0} connection"), FactoryInstance->GetSourceDisplayName());
				SourceCreationWindow = SNew(SWindow)
					.Title(CreateText)
					.ClientSize(FVector2D(400, 150))
					.SizingRule(ESizingRule::Autosized)
					.SupportsMinimize(false)
					.SupportsMaximize(false)
					[
						Widget.ToSharedRef()
					];

				GEditor->EditorAddModalWindow(SourceCreationWindow.ToSharedRef());
			}
		}
	}
}

void FLiveLinkSourcesView::ExecuteCreateSource(int32 FactoryIndex)
{
	if (Factories.IsValidIndex(FactoryIndex))
	{
		if (ULiveLinkSourceFactory* FactoryInstance = Factories[FactoryIndex])
		{
			OnSourceCreated(FactoryInstance->CreateSource(FString()), FString(), FactoryInstance->GetClass());
		}
	}
}

void FLiveLinkSourcesView::OnSourceCreated(TSharedPtr<ILiveLinkSource> NewSource, FString ConnectionString, TSubclassOf<ULiveLinkSourceFactory> Factory)
{
	if (NewSource.IsValid())
	{
		FGuid NewSourceGuid = Client->AddSource(NewSource);
		if (NewSourceGuid.IsValid())
		{
			if (ULiveLinkSourceSettings* Settings = Client->GetSourceSettings(NewSourceGuid))
			{
				Settings->ConnectionString = ConnectionString;
				Settings->Factory = Factory;
			}
		}
	}

	if (SourceCreationWindow)
	{
		SourceCreationWindow->RequestDestroyWindow();
		SourceCreationWindow.Reset();
	}
}

void FLiveLinkSourcesView::AddVirtualSubject()
{
	TSharedRef<SVirtualSubjectCreateDialog> Dialog =
		SNew(SVirtualSubjectCreateDialog)
		.LiveLinkClient(Client);

	Dialog->ConfigureVirtualSubject();
}


#undef LOCTEXT_NAMESPACE /**LiveLinkClientPanel*/
