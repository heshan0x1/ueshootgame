// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchy.h"
#include "Editor/SRigHierarchy.h"
#include "Widgets/Layout/SBox.h"
#include "Rigs/RigSpaceHierarchy.h"
#include "IStructureDetailsView.h"
#include "Misc/FrameNumber.h"
#include "EditorUndoClient.h"
#include "RigSpacePickerBakeSettings.h"
#include "SPositiveActionButton.h"

#define UE_API CONTROLRIGEDITOR_API

class SComboButton;
class IMenu;

DECLARE_DELEGATE_RetVal_TwoParams(FRigElementKey, FRigSpacePickerGetActiveSpace, URigHierarchy*, const FRigElementKey&);
DECLARE_DELEGATE_RetVal_TwoParams(const FRigControlElementCustomization*, FRigSpacePickerGetControlCustomization, URigHierarchy*, const FRigElementKey&);
DECLARE_EVENT_ThreeParams(SRigSpacePickerWidget, FRigSpacePickerActiveSpaceChanged, URigHierarchy*, const FRigElementKey&, const FRigElementKey&);
DECLARE_EVENT_ThreeParams(SRigSpacePickerWidget, FRigSpacePickerSpaceListChanged, URigHierarchy*, const FRigElementKey&, const TArray<FRigElementKeyWithLabel>&);
DECLARE_DELEGATE_RetVal_TwoParams(TArray<FRigElementKeyWithLabel>, FRigSpacePickerGetAdditionalSpaces, URigHierarchy*, const FRigElementKey&);
DECLARE_DELEGATE_RetVal_ThreeParams(FReply, SRigSpacePickerOnBake, URigHierarchy*, TArray<FRigElementKey> /* Controls */, FRigSpacePickerBakeSettings);

/** Widget allowing picking of a space source for space switching */
class SRigSpacePickerWidget : public SCompoundWidget, public FEditorUndoClient
{
public:

	SLATE_BEGIN_ARGS(SRigSpacePickerWidget)
		: _Hierarchy(nullptr)
		, _Controls()
		, _ShowDefaultSpaces(true)
		, _ShowFavoriteSpaces(true)
		, _ShowAdditionalSpaces(true)
		, _AllowReorder(false)
		, _AllowDelete(false)
		, _AllowAdd(false)
		, _ShowBakeAndCompensateButton(false)
		, _Title()
		, _BackgroundBrush(FAppStyle::GetBrush("Menu.Background"))
		{}
		SLATE_ARGUMENT(URigHierarchy*, Hierarchy)
		SLATE_ARGUMENT(TArray<FRigElementKey>, Controls)
		SLATE_ARGUMENT(bool, ShowDefaultSpaces)
		SLATE_ARGUMENT(bool, ShowFavoriteSpaces)
		SLATE_ARGUMENT(bool, ShowAdditionalSpaces)
		SLATE_ARGUMENT(bool, AllowReorder)
		SLATE_ARGUMENT(bool, AllowDelete)
		SLATE_ARGUMENT(bool, AllowAdd)
		SLATE_ARGUMENT(bool, ShowBakeAndCompensateButton)
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(const FSlateBrush*, BackgroundBrush)
	
		SLATE_EVENT(FRigSpacePickerGetActiveSpace, GetActiveSpace)
		SLATE_EVENT(FRigSpacePickerGetControlCustomization, GetControlCustomization)
		SLATE_EVENT(FRigSpacePickerActiveSpaceChanged::FDelegate, OnActiveSpaceChanged)
		SLATE_EVENT(FRigSpacePickerSpaceListChanged::FDelegate, OnSpaceListChanged)
		SLATE_ARGUMENT(FRigSpacePickerGetAdditionalSpaces, GetAdditionalSpaces)
		SLATE_EVENT(FOnClicked, OnCompensateKeyButtonClicked)
		SLATE_EVENT(FOnClicked, OnCompensateAllButtonClicked)
		SLATE_EVENT(FOnClicked, OnBakeButtonClicked )
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);
	UE_API virtual ~SRigSpacePickerWidget() override;

	UE_API void SetControls(URigHierarchy* InHierarchy, const TArray<FRigElementKey>& InControls);

	UE_API TSharedPtr<SWindow> OpenDialog(bool bModal = true);
	UE_API void CloseDialog();
	
	UE_API virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override;
	UE_API virtual bool SupportsKeyboardFocus() const override;
	UE_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	URigHierarchy* GetHierarchy() const
	{
		if (Hierarchy.IsValid())
		{
			return Hierarchy.Get();
		}
		return nullptr;
	}

	const FRigTreeDisplaySettings& GetHierarchyDisplaySettings() const { return HierarchyDisplaySettings; }
	const URigHierarchy* GetHierarchyConst() const { return GetHierarchy(); }
	
	const TArray<FRigElementKey>& GetControls() const { return ControlKeys; }
	UE_API const TArray<FRigElementKey>& GetActiveSpaces() const;
	UE_API const TArray<FRigElementKeyWithLabel>& GetDefaultSpaces() const;
	UE_API TArray<FRigElementKeyWithLabel> GetSpaceList(bool bIncludeDefaultSpaces = false) const;
	FRigSpacePickerActiveSpaceChanged& OnActiveSpaceChanged() { return ActiveSpaceChangedEvent; }
	FRigSpacePickerSpaceListChanged& OnSpaceListChanged() { return SpaceListChangedEvent; }
	UE_API void RefreshContents();

	// FEditorUndoClient interface
	UE_API virtual void PostUndo(bool bSuccess);
	UE_API virtual void PostRedo(bool bSuccess);
	// End FEditorUndoClient interface
	
private:

	enum ESpacePickerType
	{
		ESpacePickerType_Parent,
		ESpacePickerType_World,
		ESpacePickerType_Item
	};

	UE_API void AddSpacePickerRow(
		TSharedPtr<SVerticalBox> InListBox,
		ESpacePickerType InType,
		const FRigElementKey& InKey,
		const FSlateBrush* InBush,
		const FSlateColor& InColor,
		const FText& InTitle,
		FOnClicked OnClickedDelegate);

	UE_API void RepopulateItemSpaces();
	UE_API void ClearListBox(TSharedPtr<SVerticalBox> InListBox);
	UE_API void UpdateActiveSpaces();
	UE_API bool IsValidKey(const FRigElementKey& InKey) const;
	UE_API bool IsDefaultSpace(const FRigElementKey& InKey) const;

	UE_API FReply HandleParentSpaceClicked();
	UE_API FReply HandleWorldSpaceClicked();
	UE_API FReply HandleElementSpaceClicked(FRigElementKey InKey);
	UE_API FReply HandleSpaceMoveUp(FRigElementKey InKey);
	UE_API FReply HandleSpaceMoveDown(FRigElementKey InKey);
	UE_API void HandleSpaceDelete(FRigElementKey InKey);

	/** The combo button for adding items. Can be nullptr. */
	TSharedPtr<SPositiveActionButton> AddComboButton;

	/**
	 * Whether the add button menu is currently open.
	 * 
	 * Helps us decide whether to put the floating window that contains us, if any, to the front.
	 * We don't want to put the window in front if a context menu of OUR UI is opened - only when the user clicks some other UE UI.
	 */
	bool bIsAddMenuOpen = false;

	/** @return Creates the button for adding a space to the selected control. */
	TSharedRef<SWidget> CreateAddButton();
	
	/** @return Menu content for the add button */
	TSharedRef<SWidget> OnGetAddButtonContent();
	/** Handles the menu closing. Ensures the space picker widget is put on top of all UI if it is placed in the floating window.*/
	void OnMenuOpenChanged(bool bIsOpen);
	/** Assigns the clicked space to the current control rig selection */
	void HandleClickTreeItemInAddMenu(TSharedPtr<FRigTreeElement> InItem);
	
	/** @return Whether the button is enabled. Disabled e.g. when no control rig is selected. */
	bool IsAddButtonEnabled() const { return IsAddButtonEnabledWithReason(); }
	/** @return Gives the user a reason when the button is disabled. */
	FText GetAddButtonTooltipText() const;
	/** @return Whether the button should be enabled. Optionally returns a call to action text to the user. */
	bool IsAddButtonEnabledWithReason(FText* OutReason = nullptr) const;

public:
	
	UE_API bool IsRestricted() const;

private:

	UE_API bool IsSpaceMoveUpEnabled(FRigElementKey InKey) const;
	UE_API bool IsSpaceMoveDownEnabled(FRigElementKey InKey) const;

	UE_API void OnHierarchyModified(ERigHierarchyNotification InNotif, URigHierarchy* InHierarchy, const FRigNotificationSubject& InSubject);

	UE_API FSlateColor GetButtonColor(ESpacePickerType InType, FRigElementKey InKey) const;
	UE_API FRigElementKey GetActiveSpace_Private(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey) const;
	UE_API TArray<FRigElementKeyWithLabel> GetCurrentParents_Private(URigHierarchy* InHierarchy, const FRigElementKey& InControlKey) const;
	
	FRigSpacePickerActiveSpaceChanged ActiveSpaceChangedEvent;
	FRigSpacePickerSpaceListChanged SpaceListChangedEvent;

	TWeakObjectPtr<URigHierarchy> Hierarchy;
	TArray<FRigElementKey> ControlKeys;
	TArray<FRigElementKeyWithLabel> CurrentSpaceKeys;
	TArray<FRigElementKey> ActiveSpaceKeys;
	bool bRepopulateRequired;

	bool bShowDefaultSpaces;
	bool bShowFavoriteSpaces;
	bool bShowAdditionalSpaces;
	bool bAllowReorder;
	bool bAllowDelete;
	bool bAllowAdd;
	bool bShowBakeAndCompensateButton;

	FRigSpacePickerGetControlCustomization GetControlCustomizationDelegate;
	FRigSpacePickerGetActiveSpace GetActiveSpaceDelegate;
	FRigSpacePickerGetAdditionalSpaces GetAdditionalSpacesDelegate; 
	TArray<FRigElementKeyWithLabel> AdditionalSpaces;

	TSharedPtr<SVerticalBox> TopLevelListBox;
	TSharedPtr<SVerticalBox> ItemSpacesListBox;
	TSharedPtr<SHorizontalBox> BottomButtonsListBox;
	TWeakPtr<SWindow> DialogWindow;
	FDelegateHandle HierarchyModifiedHandle;
	FDelegateHandle ActiveSpaceChangedWindowHandle;
	FRigTreeDisplaySettings HierarchyDisplaySettings;

	static UE_API FRigElementKey InValidKey;

	/** Unregisters any current pending selection active timer. */
	UE_API void UnregisterPendingSelection();
	
	/** Updates the selected controls array and rebuilds the spaces list. */
	UE_API void UpdateSelection(URigHierarchy* InHierarchy, const TArray<FRigElementKey>& InControls);

	/** Controls selection currently pending. */
	TWeakPtr<FActiveTimerHandle> PendingSelectionHandle;
};

class ISequencer;

/** Widget allowing baking controls from one space to another */
class SRigSpacePickerBakeWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SRigSpacePickerBakeWidget)
	: _Hierarchy(nullptr)
	, _Controls()
	, _Sequencer(nullptr)
	{}
	SLATE_ARGUMENT(URigHierarchy*, Hierarchy)
	SLATE_ARGUMENT(TArray<FRigElementKey>, Controls)
	SLATE_ARGUMENT(ISequencer*, Sequencer)
	SLATE_EVENT(FRigSpacePickerGetControlCustomization, GetControlCustomization)
	SLATE_ARGUMENT(FRigSpacePickerBakeSettings, Settings)
	SLATE_EVENT(SRigSpacePickerOnBake, OnBake)

	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);
	virtual ~SRigSpacePickerBakeWidget() override {}

	UE_API FReply OpenDialog(bool bModal = true);
	UE_API void CloseDialog();

private:

	//used for setting up the details
	TSharedPtr<TStructOnScope<FRigSpacePickerBakeSettings>> Settings;

	FRigControlElementCustomization Customization;
	
	TWeakPtr<SWindow> DialogWindow;
	TSharedPtr<SRigSpacePickerWidget> SpacePickerWidget;
	TSharedPtr<IStructureDetailsView> DetailsView;
};

#undef UE_API
