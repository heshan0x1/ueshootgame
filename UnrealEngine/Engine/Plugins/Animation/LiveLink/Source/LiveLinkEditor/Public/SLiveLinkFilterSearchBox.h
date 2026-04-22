// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Misc/TextFilter.h"
#include "Widgets/Input/SSearchBox.h"

/**
 * Wrapper around a SSearchBox that provides utility to filter items in a list or tree view.
 */
template <typename ItemType>
class SLiveLinkFilterSearchBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnGatherItems, TArray<ItemType>& /*OutItems*/);
	DECLARE_DELEGATE_OneParam(FOnUpdateFilteredList, const TArray<ItemType>& /*FilteredItems*/);

	SLATE_BEGIN_ARGS(SLiveLinkFilterSearchBox) {}
		/** The list item for this row */
		SLATE_ARGUMENT(TArray<ItemType>*, ItemSource);
		SLATE_EVENT(FOnGatherItems, OnGatherItems);
		SLATE_EVENT(FOnUpdateFilteredList, OnUpdateFilteredList);
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SearchTextFilter = MakeShared<FEntryTextFilter>(
			FEntryTextFilter::FItemToStringArray::CreateSP(this, &SLiveLinkFilterSearchBox::GetItemString));

		ItemSource = InArgs._ItemSource;
		OnGatherItems = InArgs._OnGatherItems;
		OnUpdateFilteredList = InArgs._OnUpdateFilteredList;

		check(ItemSource);

		ChildSlot
		[
			SAssignNew(SearchBox, SSearchBox)
				.HintText(NSLOCTEXT("FilterSearchBox", "SearchHint", "Search"))
				.OnTextChanged(this, &SLiveLinkFilterSearchBox::OnSearchTextChanged)
		];
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
	{
		if (bUpdateFilteredItems)
		{
			UpdateFilteredItemSource();
			bUpdateFilteredItems = false;
		}
	}

	/** Trigger a deferred update of the filtered items. */
	void Update()
	{
		bUpdateFilteredItems = true;
	}

private:
	/** Handle search text being changed by the user, triggers a deferred update. */
	void OnSearchTextChanged(const FText& InFilterText)
	{
		SearchTextFilter->SetRawFilterText(InFilterText);
		SearchBox->SetError(SearchTextFilter->GetFilterErrorText());

		bUpdateFilteredItems = true;
	}

	/** Updates the filtered item source to match the SearchBox filter. */
	void UpdateFilteredItemSource()
	{
		FilteredItemSource.Reset();

		if (!SearchTextFilter->GetRawFilterText().IsEmpty())
		{
			// GatherChildren.Execute(ItemType);
			TArray<ItemType> SourceItems;
			OnGatherItems.Execute(SourceItems);

			Algo::TransformIf(SourceItems, FilteredItemSource,
				[this](const ItemType& Item) { return SearchTextFilter->PassesFilter(Item); },
				[](const ItemType& Item) { return Item; });
		}
		else
		{
			FilteredItemSource = *ItemSource;
		}

		OnUpdateFilteredList.ExecuteIfBound(FilteredItemSource);
	}

	/** Get string representation from a subject UI entry. */
	void GetItemString(const ItemType InItem, TArray<FString>& OutStrings)
	{
		InItem->GetFilterText(OutStrings);
	}

private:
	/** Ptr to the unfiltered item source. Used to update the filtered item list when no filtering is applied. */
	TArray<ItemType>* ItemSource = nullptr;
	/** Internal filtered item source.  */
	TArray<ItemType> FilteredItemSource;
	/** Handles deferred updates to the list filter. */
	bool bUpdateFilteredItems = true;
	/** Holds the search box widget. */
	TSharedPtr<SSearchBox> SearchBox;

	using FEntryTextFilter = TTextFilter<ItemType>;
	/** Text filter used for the subject list. */
	TSharedPtr<FEntryTextFilter> SearchTextFilter;
	
	/** 
	 * Delegate called to gather items that need to be filtered.
	 * Normally we could rely on the ItemSource ptr but for a TreeView, we may want to only display the TreeView children.
	 */
	FOnGatherItems OnGatherItems;
	/** Delegate called to the owner of this widget to inform them that the filtered list has been updated. */
	FOnUpdateFilteredList OnUpdateFilteredList;
};

