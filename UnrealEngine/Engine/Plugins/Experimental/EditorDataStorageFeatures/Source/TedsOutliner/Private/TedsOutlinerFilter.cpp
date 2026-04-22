// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerFilter.h"

#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Framework/TypedElementQueryContext.h"
#include "Styling/SlateIconFinder.h"
#include "TedsOutlinerHelpers.h"
#include "TedsOutlinerImpl.h"

#define LOCTEXT_NAMESPACE "TEDSOutlinerFilter"

namespace UE::Editor::Outliner
{
FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
	const TSharedPtr<FFilterCategory>& InCategory, const QueryHandle& InFilterQuery)
	: FilterName(InFilterName)
	, FilterDisplayName(InFilterDisplayName)
	, FilterToolTip(InFilterToolTip)
	, FilterIconName(InFilterIconName)
	, FilterCategory(InCategory)
{
	FilterQuery.Set<QueryHandle>(Helpers::CheckValidFilterQueryHandle(InFilterQuery) ? InFilterQuery : InvalidQueryHandle);
}

FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const QueryHandle& InFilterQuery)
	: FTedsFilterData(
		InFilterName, 
		InFilterDisplayName, 
		FText::FromString(InFilterName.ToString()), 
		FName(), 
		nullptr, 
		InFilterQuery)
{}

FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
	const TSharedPtr<FFilterCategory>& InCategory, const TQueryFunction<bool>& InFilterQuery)
	: FilterName(InFilterName)
	, FilterDisplayName(InFilterDisplayName)
	, FilterToolTip(InFilterToolTip)
	, FilterIconName(InFilterIconName)
	, FilterCategory(InCategory)
{
	FilterQuery.Set<TQueryFunction<bool>>(InFilterQuery);
}

FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const TQueryFunction<bool>& InFilterQuery)
	: FTedsFilterData(
		InFilterName, 
		InFilterDisplayName, 
		FText::FromString(InFilterName.ToString()), 
		FName(), 
		nullptr, 
		InFilterQuery)
{}

FTedsFilterData::FTedsFilterData(const FTedsFilterData&) = default;
FTedsFilterData::~FTedsFilterData() = default;
	
FTedsOutlinerFilter::FTedsOutlinerFilter(const FTedsFilterData& InTedsFilterData, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl)
	: FFilterBase(InTedsFilterData.FilterCategory)
	, FilterName(InTedsFilterData.FilterName)
	, FilterDisplayName(InTedsFilterData.FilterDisplayName)
	, FilterToolTip(InTedsFilterData.FilterToolTip)
	, FilterIconName(InTedsFilterData.FilterIconName)
	, bIsClassFilter(false)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
	, FilterQuery(InTedsFilterData.FilterQuery)
{}

FTedsOutlinerFilter::FTedsOutlinerFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName, 
	const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl, 
	const QueryHandle& InFilterQuery)
	: FFilterBase(InCategory)
	, FilterName(InFilterName)
	, FilterDisplayName(InFilterDisplayName)
	, FilterToolTip(InFilterToolTip)
	, FilterIconName(InFilterIconName)
	, bIsClassFilter(false)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
{
	FilterQuery.Set<QueryHandle>(Helpers::CheckValidFilterQueryHandle(InFilterQuery) ? InFilterQuery : InvalidQueryHandle);
}

FTedsOutlinerFilter::FTedsOutlinerFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, 
	const FName& InFilterIconName, const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl,
	const TQueryFunction<bool>& InFilterQuery)
	: FFilterBase(InCategory)
	, FilterName(InFilterName)
	, FilterDisplayName(InFilterDisplayName)
	, FilterToolTip(InFilterToolTip)
	, FilterIconName(InFilterIconName)
	, bIsClassFilter(false)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
{
	FilterQuery.Set<TQueryFunction<bool>>(InFilterQuery);
}

FTedsOutlinerFilter::FTedsOutlinerFilter(const UClass* InClass, const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl)
	: FFilterBase(InCategory)
	, FilterName(InClass->GetFName())
	, FilterDisplayName(InClass->GetDisplayNameText())
	, FilterToolTip(FText::Format(LOCTEXT("FilterClassTooltip", "Filter by {0}"), InClass->GetDisplayNameText()))
	, FilterIconName(FSlateIconFinder::FindIconForClass(InClass).GetStyleName())
	, bIsClassFilter(true)
	, TedsOutlinerImpl(InTedsOutlinerImpl)
{
	FilterQuery.Set<TQueryFunction<bool>>(
		Queries::BuildQueryFunction<bool>([InClass](TQueryContext<RowBatchInfo> Context, TResult<bool>& Result, TConstBatch<FTypedElementClassTypeInfoColumn> TypeInfoColumns)
		{
			Context.ForEachRow([&Result, InClass](const FTypedElementClassTypeInfoColumn& TypeInfoColumn)
			{
				Result.Add(TypeInfoColumn.TypeInfo->IsChildOf(InClass));
			}, TypeInfoColumns);
		}));
}
	
FString FTedsOutlinerFilter::GetName() const
{
	return FilterName.ToString();
}

FText FTedsOutlinerFilter::GetDisplayName() const
{
	return FilterDisplayName;
}

FText FTedsOutlinerFilter::GetToolTipText() const
{
	return FilterToolTip;
}

FLinearColor FTedsOutlinerFilter::GetColor() const
{
	return FLinearColor();	
}

FName FTedsOutlinerFilter::GetIconName() const
{
	return FilterIconName;
}

bool FTedsOutlinerFilter::IsInverseFilter() const
{
	return false;
}

void FTedsOutlinerFilter::ActiveStateChanged(bool bActive)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	const TSharedPtr<FTedsOutlinerImpl> TedsOutlinerImplPin = TedsOutlinerImpl.Pin();
	if (ensureMsgf(TedsOutlinerImplPin, TEXT("No TedsOutliner Context was set for the %s filter."), *FilterDisplayName.ToString()))
	{
		if(bActive)
		{
			if (bIsClassFilter)
			{
				// Class filters are separated since we want to OR them with each other
				TedsOutlinerImplPin->AddClassQueryFunction(FilterName, FilterQuery.Get<TQueryFunction<bool>>());
			}
			else if (FilterQuery.IsType<QueryHandle>())
			{
				TedsOutlinerImplPin->AddExternalQuery(FilterName, FilterQuery.Get<QueryHandle>());
			}
			else /* if (FilterQuery.IsType<TQueryFunction<bool>>()) */
			{
				TedsOutlinerImplPin->AddExternalQueryFunction(FilterName, FilterQuery.Get<TQueryFunction<bool>>());
			}
		}
		else
		{
			if (bIsClassFilter)
			{
				TedsOutlinerImplPin->RemoveClassQueryFunction(FilterName);
			}
			else if (FilterQuery.IsType<QueryHandle>())
			{
				TedsOutlinerImplPin->RemoveExternalQuery(FilterName);
			}
			else /* if (FilterQuery.IsType<TQueryFunction<bool>>()) */
			{
				TedsOutlinerImplPin->RemoveExternalQueryFunction(FilterName);
			}
		}
	}
}

void FTedsOutlinerFilter::ModifyContextMenu(FMenuBuilder& MenuBuilder)
{
	
}

void FTedsOutlinerFilter::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	
}

void FTedsOutlinerFilter::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	
}

bool FTedsOutlinerFilter::PassesFilter(SceneOutliner::FilterBarType InItem) const
{
	if (TSharedPtr<FTedsOutlinerImpl> TedsOutlinerImplPin = TedsOutlinerImpl.Pin())
	{
		// If this item is not compatible with the owning Table Viewer - it does not pass any filter queries
		// If it is compatible, this is simply a dummy filter for the UI while the actual filter is applied through the TEDS query
		if(TedsOutlinerImplPin->IsItemCompatible().IsBound())
		{
			return TedsOutlinerImplPin->IsItemCompatible().Execute(InItem);
		}
	}

	// The filter is applied through a TEDS query and this is just a dummy to activate it, so we can simply return false otherwise
	return false;
}
} // namespace UE::Editor::Outliner

#undef LOCTEXT_NAMESPACE
