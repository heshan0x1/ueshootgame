// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementQueryContextImplementation.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"

struct FMassExecutionContext;

namespace UE::Editor::DataStorage::Queries
{
	/** Query context implementation for single row queries that call into TEDS Direct API. */
	class FQueryContext_MassForwarder final : public SingleRowInfo, public RowBatchInfo, public IQueryFunctionResponse
	{
	public:
		static const FName Name;

		explicit FQueryContext_MassForwarder(FMassExecutionContext& Context);

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
		virtual bool CurrentBatchTableHasColumns(TConstArrayView<const UScriptStruct*> ColumnTypes) const override;

		/**
		 * @section IQueryFunctionResponse
		 */
		virtual bool NextBatch() override;
		virtual bool NextRow() override;
		virtual void GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override;
		virtual void GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override;

	private:
		FMassExecutionContext& Context;
		const RowHandle* CurrentRow;
		const RowHandle* EndRow;
	};
	
	using QueryContext_MassForwarder = TQueryContextImpl<FQueryContext_MassForwarder>;
} // namespace UE::Editor::DataStorage::Queries
