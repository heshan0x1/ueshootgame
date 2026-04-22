// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

#include "Filters/FilterBase.h"
#include "DataStorage/Features.h"
#include "DataStorage/Handles.h"
#include "DataStorage/Queries/Description.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Framework/TypedElementQueryContext.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "TEDSFilter"

namespace UE::Editor::DataStorage
{
	class STedsFilterBar;

	// Helper function to check if the given QueryHandle is valid to be used in a filter (is not an observer).
	inline bool CheckValidFilterQueryHandle(const QueryHandle& InQueryHandle)
	{
		// Check if the given Query Handle is valid for a filter (Not an Observer Query Handle)
		DataStorage::ICoreProvider* Storage = GetMutableDataStorageFeature<DataStorage::ICoreProvider>(StorageFeatureName);
		if (ensureMsgf(Storage, TEXT("TEDS must be initialized before TEDS Filters")))
		{
			const EQueryCallbackType QueryHandleCallbackType = Storage->GetQueryDescription(InQueryHandle).Callback.Type;
			return ensureMsgf(QueryHandleCallbackType == EQueryCallbackType::None || QueryHandleCallbackType == EQueryCallbackType::Processor,
				TEXT("TEDS Filters cannot accept Observer Query Handles."));
		}
		return false;
	}

	struct TEDSTABLEVIEWER_API FTedsFilterData
	{
		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, const QueryHandle& InFilterQuery);

		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const QueryHandle& InFilterQuery);
		
		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, const Queries::TQueryFunction<bool>& InFilterQuery);

		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const Queries::TQueryFunction<bool>& InFilterQuery);

		/*
		 * Require that the template type cannot be one of the pre-defined FilterQuery types so it will pass to the defined
		 * constructor instead of this one.
		 */
		template< typename FilterFunction UE_REQUIRES(
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, QueryHandle> &&
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, Queries::TQueryFunction<bool>>) >
		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, FilterFunction&& InFilterQuery)
			: FTedsFilterData(
				InFilterName, 
				InFilterDisplayName, 
				InFilterToolTip, 
				InFilterIconName, 
				InCategory,
				Queries::BuildQueryFunction<bool>(Forward<FilterFunction>(InFilterQuery)))
		{}

		/*
		 * Require that the template type cannot be one of the pre-defined FilterQuery types so it will pass to the defined
		 * constructor instead of this one.
		 */
		template< typename FilterFunction UE_REQUIRES(
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, QueryHandle> &&
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, Queries::TQueryFunction<bool>>) >
	    FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, FilterFunction&& InFilterQuery)
			: FTedsFilterData(
				InFilterName, 
				InFilterDisplayName, 
				FText::FromString(InFilterName.ToString()), 
				FName(), 
				nullptr, 
				Queries::BuildQueryFunction<bool>(Forward<FilterFunction>(InFilterQuery)))
		{}

		FTedsFilterData(const FTedsFilterData&);
		~FTedsFilterData(); 
		
		FName FilterName;
		FText FilterDisplayName;
		FText FilterToolTip;
		FName FilterIconName;
		
		// The Category can be set as a nullptr in FTedsFilterData to go to the default 'Other Filters' category when adding,
		// otherwise the filter will not display if it's not given a category
		TSharedPtr<FFilterCategory> FilterCategory;
		TVariant<QueryHandle, Queries::TQueryFunction<bool>> FilterQuery;
	};
		
	class FTedsFilter : public FFilterBase<FTedsRowHandle&>
	{
	public:
		FTedsFilter(const FTedsFilterData& InTedsFilterData, const TSharedPtr<STedsFilterBar>& InTedsFilterBar )
			: FFilterBase(InTedsFilterData.FilterCategory)
			, FilterName(InTedsFilterData.FilterName)
			, FilterDisplayName(InTedsFilterData.FilterDisplayName)
			, FilterToolTip(InTedsFilterData.FilterToolTip)
			, FilterIconName(InTedsFilterData.FilterIconName)
			, bIsClassFilter(false)
			, FilterQuery(InTedsFilterData.FilterQuery)
			, TedsFilterBar(InTedsFilterBar)
		{}

		FTedsFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName, 
			const TSharedPtr<FFilterCategory>& InCategory, const QueryHandle& InFilterQuery, const TSharedPtr<STedsFilterBar>& InTedsFilterBar)
			: FFilterBase(InCategory)
			, FilterName(InFilterName)
			, FilterDisplayName(InFilterDisplayName)
			, FilterToolTip(InFilterToolTip)
			, FilterIconName(InFilterIconName)
			, bIsClassFilter(false)
			, TedsFilterBar(InTedsFilterBar)
		{
			FilterQuery.Set<QueryHandle>(CheckValidFilterQueryHandle(InFilterQuery) ? InFilterQuery : InvalidQueryHandle);
		}
		
		FTedsFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, 
			const FName& InFilterIconName, const TSharedPtr<FFilterCategory>& InCategory, const Queries::TQueryFunction<bool>& InFilterQuery, 
			const TSharedPtr<STedsFilterBar>& InTedsFilterBar)
			: FFilterBase(InCategory)
			, FilterName(InFilterName)
			, FilterDisplayName(InFilterDisplayName)
			, FilterToolTip(InFilterToolTip)
			, FilterIconName(InFilterIconName)
			, bIsClassFilter(false)
			, TedsFilterBar(InTedsFilterBar)
		{
			FilterQuery.Set<Queries::TQueryFunction<bool>>(InFilterQuery);
		}

		FTedsFilter(const UClass* InClass, const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<STedsFilterBar>& InTedsFilterBar)
			: FFilterBase(InCategory)
			, FilterName(InClass->GetFName())
			, FilterDisplayName(InClass->GetDisplayNameText())
			, FilterToolTip(FText::Format(LOCTEXT("FilterClassTooltip", "Filter by {0}"), InClass->GetDisplayNameText()))
			, FilterIconName(FSlateIconFinder::FindIconForClass(InClass).GetStyleName())
			, bIsClassFilter(true)
			, TedsFilterBar(InTedsFilterBar)
		{
			FilterQuery.Set<Queries::TQueryFunction<bool>>(
				Queries::BuildQueryFunction<bool>([InClass](
					Queries::TQueryContext<Queries::RowBatchInfo> Context,
					Queries::TResult<bool>& Result,
					Queries::TConstBatch<FTypedElementClassTypeInfoColumn> TypeInfoColumns)
				{
					Context.ForEachRow([&Result, InClass](const FTypedElementClassTypeInfoColumn& TypeInfoColumn)
					{
						Result.Add(TypeInfoColumn.TypeInfo->IsChildOf(InClass));
					}, TypeInfoColumns);
				}));
		}

		/** Returns the system name for this filter */
		virtual FString GetName() const override;

		/** Returns the human-readable name for this filter */
		virtual FText GetDisplayName() const override;

		/** Returns the tooltip for this filter, shown in the filters menu */
		virtual FText GetToolTipText() const override;

		/** Returns the color this filter button will be when displayed as a button */
		virtual FLinearColor GetColor() const override;

		/** Returns the name of the icon to use in menu entries */
		virtual FName GetIconName() const override;

		/** If true, the filter will be active in the FilterBar when it is inactive in the UI (i.e the filter pill is grayed out) */
		virtual bool IsInverseFilter() const override;

		/** Notification that the filter became active or inactive */
		virtual void ActiveStateChanged(bool bActive) override;

		/** Called when the right-click context menu is being built for this filter */
		virtual void ModifyContextMenu(FMenuBuilder& MenuBuilder) override;
		
		/** Can be overriden for custom FilterBar subclasses to save settings, currently not implemented in any generic Filter Bar */
		virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override;

		/** Can be overridden for custom FilterBar subclasses to load settings, currently not implemented in any generic Filter Bar */
		virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override;
		
		/** Returns whether the specified Item passes the Filter's restrictions */
		virtual bool PassesFilter(FTedsRowHandle& InItem ) const override;

	protected:

		FName FilterName;
		FText FilterDisplayName;
		FText FilterToolTip;
		FName FilterIconName;

		const bool bIsClassFilter;

		TVariant<QueryHandle, Queries::TQueryFunction<bool>> FilterQuery;
		TWeakPtr<STedsFilterBar> TedsFilterBar;
	};
} // namespace UE::Editor::DataStorage

#undef LOCTEXT_NAMESPACE