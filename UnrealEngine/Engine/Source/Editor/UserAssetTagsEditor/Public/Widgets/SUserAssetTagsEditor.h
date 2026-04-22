// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UserAssetTagProvider.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetRegistry/AssetData.h"
#include "Templates/SharedPointer.h"
#include "UserAssetTagEditorMenuContexts.h"

struct FToolMenuContext;
struct FToolUIAction;
struct FToolMenuCustomWidgetContext;

/** Optional info relating to a specific tag, useful to surface more information to the user. */
struct FUserAssetTagInfo
{
	/** A tag can come from multiple sources at once. */
	TArray<const UUserAssetTagProvider*> Sources;
};

/** The actual editor widget to assign/unassign tags. */
class USERASSETTAGSEDITOR_API SUserAssetTagsEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUserAssetTagsEditor)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);
	virtual ~SUserAssetTagsEditor() override;
	
	const TArray<TSharedPtr<FAssetData>>& GetSelectedAssets() const { return SelectedAssets; }

	void RefreshDataAndMenus();
	static void RefreshMenus();
	static void RefreshSelectedAssetsMenu();
	static void RefreshSuggestedTagMenus();
	static void RefreshOwnedTagMenus();
	static void RefreshProviderExtensionToolbarMenu();

private:
	TSharedPtr<SWidget> CreateSelectedAssetList();
	TSharedPtr<SWidget> CreateSuggestedTagList();
	TSharedPtr<SWidget> CreateOwnedTagsList();
	TSharedPtr<SWidget> CreateToolbar();

	static TArray<FAssetData> GetCurrentContentBrowserAssetSelection();
	
	void OnCommitNewTag(const FText& Text, ETextCommit::Type CommitType);
	FReply OnAddTagButtonClicked();
	bool IsAddTagButtonEnabled() const;

	const FSlateBrush* GetViewOptionsBadgeIcon() const;
	
	static TSharedRef<SWidget> GenerateRowContent_SelectedAsset(const FAssetData& AssetData, const UUserAssetTagEditorContext* Context);
	static TSharedRef<SWidget> GenerateRowContent_SuggestedTag(FName UserAssetTag, const UUserAssetTagEditorContext* Context);
	static TSharedRef<SWidget> GenerateRowContent_OwnedTag(FName UserAssetTag, const UUserAssetTagEditorContext* Context);
	static TSharedRef<SWidget> CreateMenuControlWidget(const FToolMenuContext& ToolMenuContext, const FToolMenuCustomWidgetContext& ToolMenuCustomWidgetContext, const UClass* Class);

	TSharedRef<SWidget> OnGetViewOptions();
	static void ToggleSortByAlphabet(const FToolMenuContext& ToolMenuContext);
	static ECheckBoxState GetShouldSortByAlphabet(const FToolMenuContext& ToolMenuContext);
	static void ToggleViewOption_ProviderClass(const FToolMenuContext& ToolMenuContext, const UClass* ProviderClass);
	static ECheckBoxState GetViewOptions_IsProviderClassEnabled(const FToolMenuContext& ToolMenuContext, const UClass* Class);

	static void SetViewOption_ProviderClassMenuType(EUserAssetTagProviderMenuType InMenuType, const UClass* ProviderClass);
	static EUserAssetTagProviderMenuType GetViewOptions_ProviderClassMenuType(const UClass* ProviderClass);
	
	static const TArray<const UUserAssetTagProvider*>& GetAllProviderCDOs();
	static TArray<const UUserAssetTagProvider*> GetAllValidDefaultEnabledProviderCDOs(const UUserAssetTagEditorContext* Context);
	static TArray<const UUserAssetTagProvider*> GetValidProviderCDOs(const UUserAssetTagEditorContext* Context);
	static TArray<const UUserAssetTagProvider*> GetEnabledProviderCDOs(const UUserAssetTagEditorContext* Context);

	static FToolUIAction CreateToggleTagCheckboxAction_SuggestedTag(const FName& InUserAssetTag);
	static FToolUIAction CreateToggleTagCheckboxAction_OwnedTag(const FName& InUserAssetTag);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	static FReply OnDocumentationRequested();
	static FText GetDocumentationButtonTooltipText();

	static FReply OnDeleteTagClicked(FName InUserAssetTag, const UUserAssetTagEditorContext* Context);

	void HandleAssetSelectionChanged(const TArray<FAssetData>& AssetData, bool bIsPrimaryBrowser);
	
	static TArray<FAssetData> TransformAssetData(const TArray<TSharedPtr<FAssetData>>& InAssetData);
private:
	TSharedPtr<SWidget> SelectedAssetList;
	TSharedPtr<SWidget> TagSuggestionList;
	TSharedPtr<SWidget> OwnedTagsList;

	TArray<TSharedPtr<FAssetData>> SelectedAssets;
	
	TSharedPtr<class SEditableTextBox> AddTagTextBox;
	TSharedPtr<SWidget> ToolbarWidget;

	/** The editor relies on UToolMenus a lot. We construct a context object once, and then pass it around to mostly static functions that generate the UI. */
	TStrongObjectPtr<UUserAssetTagEditorContext> ThisContext;

private:
	static TArray<const UUserAssetTagProvider*> CachedProviderCDOs;
};
