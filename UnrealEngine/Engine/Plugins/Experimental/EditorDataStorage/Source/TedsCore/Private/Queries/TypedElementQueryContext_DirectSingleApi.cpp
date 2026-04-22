// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/TypedElementQueryContext_DirectSingleApi.h"
#include "TypedElementDatabase.h"

namespace UE::Editor::DataStorage::Queries
{
	const FName FQueryContext_DirectSingleApi::Name = "FQueryContext_DirectSingleApi";

	FQueryContext_DirectSingleApi::FQueryContext_DirectSingleApi(UEditorDataStorage& DataStorage)
		: DataStorage(DataStorage)
	{
	}

	bool FQueryContext_DirectSingleApi::CurrentRowHasColumns(TConstArrayView<const UScriptStruct*> ColumnTypes) const
	{
		return DataStorage.HasColumns(CurrentRow, ColumnTypes);
	}

	RowHandle FQueryContext_DirectSingleApi::GetCurrentRow() const
	{
		return CurrentRow;
	}

	void FQueryContext_DirectSingleApi::SetCurrentRow(RowHandle Row)
	{
		CurrentRow = Row;
	}

	bool FQueryContext_DirectSingleApi::NextBatch()
	{
		return false; // Only operates on a single row, so there are no more batches.
	}

	bool FQueryContext_DirectSingleApi::NextRow()
	{
		return false; // Only operates on a single row, so there are no more rows.
	}

	void FQueryContext_DirectSingleApi::GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		const void** ColumnsDataIt = ColumnsData.GetData();
		const UEditorDataStorage& ConstDataStorage = DataStorage;
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			*ColumnsDataIt++ = DataStorage.GetColumnData(CurrentRow, ColumnType);
		}
	}

	void FQueryContext_DirectSingleApi::GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes)
	{
		void** ColumnsDataIt = ColumnsData.GetData();
		for (const UScriptStruct* ColumnType : ColumnTypes)
		{
			*ColumnsDataIt++ = DataStorage.GetColumnData(CurrentRow, ColumnType);
		}
	}

	uint32 FQueryContext_DirectSingleApi::GetBatchRowCount() const
	{
		return 1;
	}

	TConstArrayView<RowHandle> FQueryContext_DirectSingleApi::GetBatchRowHandles() const
	{
		return TConstArrayView<RowHandle>(&CurrentRow, 1);
	}

	bool FQueryContext_DirectSingleApi::CurrentBatchTableHasColumns(TConstArrayView<const UScriptStruct*> Columns) const
	{
		return CurrentRowHasColumns(Columns);
	}

} // namespace UE::Editor::DataStorage::Queries