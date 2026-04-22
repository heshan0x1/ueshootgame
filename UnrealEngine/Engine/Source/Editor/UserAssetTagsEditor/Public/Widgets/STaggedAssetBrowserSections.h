// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TaggedAssetBrowserFilters/TaggedAssetBrowser_CommonFilters.h"
#include "Widgets/SCompoundWidget.h"

class UTaggedAssetBrowserSection;
class UTaggedAssetBrowserFilterRoot;
/**
 * 
 */

class STaggedAssetBrowserSection : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STaggedAssetBrowserSection)
		{}
		SLATE_EVENT(FOnCheckStateChanged, OnCheckStateChanged)
		SLATE_ATTRIBUTE(ECheckBoxState, IsChecked)
		SLATE_EVENT(FOnGetContent, OnGetMenuContent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UTaggedAssetBrowserSection& InSection);

private:
	void OnCheckStateChanged(ECheckBoxState CheckBoxState);
	ECheckBoxState GetCheckState() const;
	TSharedRef<SWidget> OnGetMenuContent();
	FSlateColor GetIconForegroundColor() const;
	const FSlateBrush* GetIconBrush() const;
	EVisibility GetLabelVisibility() const;
	FText GetLabelText() const;

private:
	TWeakObjectPtr<const UTaggedAssetBrowserSection> Section;

	FOnCheckStateChanged OnCheckStateChangedDelegate;
	TAttribute<ECheckBoxState> IsCheckedAttribute;
	FOnGetContent OnGetMenuContentDelegate;
};

class USERASSETTAGSEDITOR_API STaggedAssetBrowserSections : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSectionSelected, const UTaggedAssetBrowserSection* Section)
	
	SLATE_BEGIN_ARGS(STaggedAssetBrowserSections)
		: _InitiallyActiveSection(nullptr)
		{}
		SLATE_ARGUMENT(const UTaggedAssetBrowserSection*, InitiallyActiveSection)
		SLATE_EVENT(FOnSectionSelected, OnSectionSelected)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const UTaggedAssetBrowserFilterRoot& InFilterRoot);

	void RebuildWidget();

	const UTaggedAssetBrowserSection* GetActiveSection() const { return ActiveSection.Get(); }

private:
	void OnSectionSelected(ECheckBoxState CheckBoxState, const UTaggedAssetBrowserSection* InSection);
	ECheckBoxState IsSectionActive(const UTaggedAssetBrowserSection* InSection) const;

private:
	TWeakObjectPtr<const UTaggedAssetBrowserFilterRoot> FilterRoot;

	FOnSectionSelected OnSectionSelectedDelegate;

	TWeakObjectPtr<const UTaggedAssetBrowserSection> ActiveSection;
	TSharedPtr<SScrollBox> ScrollBox;

};
