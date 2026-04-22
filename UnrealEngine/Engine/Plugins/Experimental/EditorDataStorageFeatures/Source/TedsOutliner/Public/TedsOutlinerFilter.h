// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include <type_traits>
#include "Filters/FilterBase.h"
#include "SceneOutlinerPublicTypes.h"
#include "DataStorage/Queries/Description.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

namespace UE::Editor::Outliner
{
	using namespace UE::Editor::DataStorage::Queries;
	class FTedsOutlinerImpl;

	struct TEDSOUTLINER_API FTedsFilterData
	{
		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, const QueryHandle& InFilterQuery);

		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const QueryHandle& InFilterQuery);
		
		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, const TQueryFunction<bool>& InFilterQuery);

		FTedsFilterData(const FName& InFilterName, const FText& InFilterDisplayName, const TQueryFunction<bool>& InFilterQuery);

		/*
		 * Require that the template type cannot be one of the pre-defined FilterQuery types so it will pass to the defined
		 * constructor instead of this one.
		 */
		template< typename FilterFunction UE_REQUIRES(
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, QueryHandle> &&
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, TQueryFunction<bool>>) >
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
			!std::is_same_v<std::remove_cvref_t<FilterFunction>, TQueryFunction<bool>>) >
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
		
		// The Category can be set as a nullptr in FTedsFilterData to go to the default 'Other Filters' category when adding via the 
		// FTedsOutlinerParams.Filters list, otherwise the filter will not display if it's not given a category
		TSharedPtr<FFilterCategory> FilterCategory;
		TVariant<QueryHandle, TQueryFunction<bool>> FilterQuery;
	};
		
	class FTedsOutlinerFilter : public FFilterBase<SceneOutliner::FilterBarType>
	{
	public:
		FTedsOutlinerFilter(const FTedsFilterData& InTedsFilterData, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl);

		FTedsOutlinerFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName, 
			const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl, const QueryHandle& InFilterQuery);
		
		FTedsOutlinerFilter(const FName& InFilterName, const FText& InFilterDisplayName, const FText& InFilterToolTip, const FName& InFilterIconName,
			const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl, const TQueryFunction<bool>& InFilterQuery);

		FTedsOutlinerFilter(const UClass* InClass, const TSharedPtr<FFilterCategory>& InCategory, const TSharedPtr<FTedsOutlinerImpl>& InTedsOutlinerImpl);

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
		virtual bool PassesFilter(SceneOutliner::FilterBarType InItem ) const override;

	protected:

		FName FilterName;
		FText FilterDisplayName;
		FText FilterToolTip;
		FName FilterIconName;

		const bool bIsClassFilter;

		TWeakPtr<FTedsOutlinerImpl> TedsOutlinerImpl;
		TVariant<QueryHandle, TQueryFunction<bool>> FilterQuery;
	};
} // namespace UE::Editor::Outliner

#endif
