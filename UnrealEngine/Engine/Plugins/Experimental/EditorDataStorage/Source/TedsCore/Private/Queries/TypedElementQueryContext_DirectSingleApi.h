// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementQueryContextImplementation.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"

class UEditorDataStorage;

namespace UE::Editor::DataStorage::Queries
{
	/** 
	 * Query context implementation for single row queries that call into TEDS Direct API.
	 * Note: batch operations, for instance RowBatchInfo, are supported by this context, but
	 * will in practice only run a single row. Batch mode was added for compatibility and offers
	 * no performance benefits when using this context.
	 */
	class FQueryContext_DirectSingleApi final : public SingleRowInfo, public RowBatchInfo, public IQueryFunctionResponse
	{
	public:
		static const FName Name;

		explicit FQueryContext_DirectSingleApi(UEditorDataStorage& DataStorage);

		/**
		 * @section ISingleRowInfo
		 */

		virtual bool CurrentRowHasColumns(TConstArrayView<const UScriptStruct*> ColumnTypes) const override;
		virtual RowHandle GetCurrentRow() const override;

		/**
		 * @section IRowBatchInfo
		 */
		virtual uint32 GetBatchRowCount() const override;
		virtual TConstArrayView<RowHandle> GetBatchRowHandles() const override;
		virtual bool CurrentBatchTableHasColumns(TConstArrayView<const UScriptStruct*> Columns) const override;

		/**
		 * @section IQueryFunctionResponse
		 */
		virtual bool NextBatch() override;
		virtual bool NextRow() override;
		virtual void GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override;
		virtual void GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override;

		void SetCurrentRow(RowHandle Row);

	private:
		UEditorDataStorage& DataStorage;
		RowHandle CurrentRow = InvalidRowHandle;
	};
	
	using QueryContext_DirectSingleApi = TQueryContextImpl<FQueryContext_DirectSingleApi>;
} // namespace UE::Editor::DataStorage::Queries
