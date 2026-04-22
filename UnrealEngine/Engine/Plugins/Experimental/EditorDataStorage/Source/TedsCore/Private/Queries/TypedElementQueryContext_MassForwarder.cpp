// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/TypedElementQueryContext_MassForwarder.h"
#include "MassExecutionContext.h"
#include "TypedElementUtils.h"

namespace UE::Editor::DataStorage::Queries
{
	const FName FQueryContext_MassForwarder::Name = "FQueryContext_MassForwarder";

	FQueryContext_MassForwarder::FQueryContext_MassForwarder(FMassExecutionContext& Context)
		: Context(Context)
	{
		TConstArrayView<RowHandle> Rows = MassEntitiesToRowsConversion(Context.GetEntities());
		CurrentRow = Rows.GetData();
		EndRow = Rows.GetData() + Rows.Num();
	}

	bool FQueryContext_MassForwarder::CurrentRowHasColumns(TConstArrayView<const UScriptStruct*> ColumnTypes) const
	{
		bool Result = true;
		const UScriptStruct* const* It = ColumnTypes.GetData();
		const UScriptStruct* const* End = ColumnTypes.GetData() + ColumnTypes.Num();

		for (; Result && It != End; ++It)
		{
			const UScriptStruct* ColumnType = *It;
			if (UE::Mass::IsA<FMassTag>(ColumnType))
			{
				Result = Context.DoesArchetypeHaveTag(*ColumnType);
			}
			else if (UE::Mass::IsA<FMassFragment>(ColumnType))
			{
				Result = Context.DoesArchetypeHaveFragment(*ColumnType);
			}
			else
			{
				const bool bIsTagOrFragment = false;
				checkf(bIsTagOrFragment, TEXT("Attempting to check for a column type that is not a column or tag."));
				return false;
			}
		}
		return Result;
	}

	RowHandle FQueryContext_MassForwarder::GetCurrentRow() const
	{
		checkfSlow(CurrentRow != nullptr, TEXT("GetCurrentRow shouldn't be called if there are no rows."));
		return *CurrentRow;
	}

	uint32 FQueryContext_MassForwarder::GetBatchRowCount() const
	{
		return static_cast<uint32>(Context.GetNumEntities());
	}

	TConstArrayView<RowHandle> FQueryContext_MassForwarder::GetBatchRowHandles() const
	{
		return MassEntitiesToRowsConversion(Context.GetEntities());
	}

	bool FQueryContext_MassForwarder::CurrentBatchTableHasColumns(TConstArrayView<const UScriptStruct*> ColumnTypes) const
	{
		return CurrentRowHasColumns(ColumnTypes);
	}

	bool FQueryContext_MassForwarder::NextBatch()
	{
		return false; // This only ever contains a single batch.
	}

	bool FQueryContext_MassForwarder::NextRow()
	{
		checkfSlow(CurrentRow != nullptr, TEXT("NextRow shouldn't be called if there are no rows."));
		CurrentRow++;
		return CurrentRow < EndRow;
	}

	void FQueryContext_MassForwarder::GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		checkfSlow(ColumnsData.Num() >= ColumnTypes.Num(), TEXT("The list size of requested const column types (%i) doesn't match the "
			"available space for the results (%i)"), ColumnTypes.Num(), ColumnsData.Num());

		const void** ColumnsDataIt = ColumnsData.GetData();
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			checkfSlow(UE::Mass::IsA<FMassFragment>(ColumnType),
				TEXT("Unable to retrieve const column data for a column type without data: %s"), *ColumnType->GetFullName());
			*ColumnsDataIt++ = Context.GetFragmentView(ColumnType).GetData();
		}
	}

	void FQueryContext_MassForwarder::GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		checkfSlow(ColumnsData.Num() >= ColumnTypes.Num(), TEXT("The list size of requested mutable column types (%i) doesn't match the "
			"available space for the results (%i)"), ColumnTypes.Num(), ColumnsData.Num());

		void** ColumnsDataIt = ColumnsData.GetData();
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			checkfSlow(UE::Mass::IsA<FMassFragment>(ColumnType),
				TEXT("Unable to retrieve mutable column data for a column type without data: %s"), *ColumnType->GetFullName());
			*ColumnsDataIt++ = Context.GetMutableFragmentView(ColumnType).GetData();
		}
	}
} // namespace UE::Editor::DataStorage::Queries