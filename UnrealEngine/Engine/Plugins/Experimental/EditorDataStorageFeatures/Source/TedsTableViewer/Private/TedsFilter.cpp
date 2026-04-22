// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsFilter.h"

#include "Widgets/STedsFilterBar.h"

namespace UE::Editor::DataStorage
{
	class STedsFilterBar;

	FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
    	const TSharedPtr<FFilterCategory>& InCategory, const QueryHandle& InFilterQuery)
    	: FilterName(InFilterName)
    	, FilterDisplayName(InFilterDisplayName)
    	, FilterToolTip(InFilterToolTip)
    	, FilterIconName(InFilterIconName)
    	, FilterCategory(InCategory)
    {
    	FilterQuery.Set<QueryHandle>(CheckValidFilterQueryHandle(InFilterQuery) ? InFilterQuery : InvalidQueryHandle);
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
    	const TSharedPtr<FFilterCategory>& InCategory, const Queries::TQueryFunction<bool>& InFilterQuery)
    	: FilterName(InFilterName)
    	, FilterDisplayName(InFilterDisplayName)
    	, FilterToolTip(InFilterToolTip)
    	, FilterIconName(InFilterIconName)
    	, FilterCategory(InCategory)
    {
    	FilterQuery.Set<Queries::TQueryFunction<bool>>(InFilterQuery);
    }
    
    FTedsFilterData::FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const Queries::TQueryFunction<bool>& InFilterQuery)
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
		
	FString FTedsFilter::GetName() const
	{
		return FilterName.ToString();
	}

	FText FTedsFilter::GetDisplayName() const
	{
		return FilterDisplayName;
	}

	FText FTedsFilter::GetToolTipText() const
	{
		return FilterToolTip;
	}

	FLinearColor FTedsFilter::GetColor() const
	{
		return FLinearColor();	
	}

	FName FTedsFilter::GetIconName() const
	{
		return FilterIconName;
	}

	bool FTedsFilter::IsInverseFilter() const
	{
		return false;
	}

	void FTedsFilter::ActiveStateChanged(bool bActive)
	{
		using namespace UE::Editor::DataStorage::Queries;
		
		const TSharedPtr<STedsFilterBar> TedsFilterBarPin = TedsFilterBar.Pin();
		if (ensureMsgf(TedsFilterBarPin, TEXT("No TedsFilterBar Context was set for the %s filter."), *FilterDisplayName.ToString()))
		{
			if(bActive)
			{
				if (bIsClassFilter)
				{
					// Class filters are separated since we want to OR them with each other
					TedsFilterBarPin->AddClassQueryFunction(FilterName, FilterQuery.Get<TQueryFunction<bool>>());
				}
				else if (FilterQuery.IsType<QueryHandle>())
				{
					TedsFilterBarPin->AddExternalQuery(FilterName, FilterQuery.Get<QueryHandle>());
				}
				else /* if (FilterQuery.IsType<TQueryFunction<bool>>()) */
				{
					TedsFilterBarPin->AddExternalQueryFunction(FilterName, FilterQuery.Get<TQueryFunction<bool>>());
				}
			}
			else
			{
				if (bIsClassFilter)
				{
					TedsFilterBarPin->RemoveClassQueryFunction(FilterName);
				}
				else if (FilterQuery.IsType<QueryHandle>())
				{
					TedsFilterBarPin->RemoveExternalQuery(FilterName);
				}
				else /* if (FilterQuery.IsType<TQueryFunction<bool>>()) */
				{
					TedsFilterBarPin->RemoveExternalQueryFunction(FilterName);
				}
			}
		}
	}

	void FTedsFilter::ModifyContextMenu(FMenuBuilder& MenuBuilder)
	{
		
	}

	void FTedsFilter::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
	{
		
	}

	void FTedsFilter::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
	{
		
	}

	bool FTedsFilter::PassesFilter(FTedsRowHandle& InItem) const
	{
		// The filter is applied through a TEDS query and this is just a dummy to activate it, so we can just run a simple valid check on if the
		// TEDSFilterBar is still valid.
		return TedsFilterBar.IsValid();
	}
} // namespace UE::Editor::DataStorage
