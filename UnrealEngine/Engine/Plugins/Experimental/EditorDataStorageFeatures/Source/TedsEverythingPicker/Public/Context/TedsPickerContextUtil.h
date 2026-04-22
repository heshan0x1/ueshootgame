// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/CommonTypes.h"
#include "Context/TedsPickerContext.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

#include "Widgets/SCompoundWidget.h"

#define UE_API TEDSEVERYTHINGPICKER_API

namespace UE::Editor::DataStorage::QueryStack
{
	class FRowFilterNode;
	class IRowNode;
}

namespace UE::Editor::DataStorage::Picker
{
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, RowHandle);

	class SObjectReferenceContextView : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SObjectReferenceContextView)
			: _SearchingEnabled(false)
			{}
			SLATE_ARGUMENT(FQueryDescription, Query)
			SLATE_ARGUMENT(TSharedPtr<QueryStack::IRowNode>, QueryStack) // Advanced: QueryStack allows finer control over displayed data. Will be used instead of Query if specified
			SLATE_ARGUMENT(FOnSelectionChanged, OnSelectionChanged)
			SLATE_ARGUMENT(bool, SearchingEnabled)
		SLATE_END_ARGS()

		UE_API void Construct(const FArguments& InArgs);
	};

	class STypeListContextView : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STypeListContextView)
			: _SearchingEnabled(false)
			{}
			SLATE_ARGUMENT(FQueryDescription, Query) // If set will perform the requested query. Ex: Select().Where().All<FClassTypeInfoTag>().Compile(). Results Intersect with BaseType
			SLATE_ARGUMENT(TSharedPtr<QueryStack::IRowNode>, QueryStack) // Advanced: QueryStack allows finer control over displayed data. Will be used instead of Query if specified
			SLATE_ARGUMENT(UStruct*, BaseType) // If set, will collect a list of BaseType + SubTypes. Results intersect with Query
			SLATE_ARGUMENT(FOnSelectionChanged, OnSelectionChanged)
			SLATE_ARGUMENT(bool, SearchingEnabled)
		SLATE_END_ARGS()

		UE_API void Construct(const FArguments& InArgs);
	};
}

#undef UE_API
