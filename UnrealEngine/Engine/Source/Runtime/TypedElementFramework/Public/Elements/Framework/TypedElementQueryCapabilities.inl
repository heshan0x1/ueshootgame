// Copyright Epic Games, Inc. All Rights Reserved.

// Mini-DSL to declare TEDS capabilities.
// Do not include #pragma once here or any other guards against repeated inclusion as this file is meant to be included multiple times.
// This file contains the definitions of all available query capabilities. The macros get expended in various ways to generate interfaces,
// function forwarders, contexts, etc.

/**
 * Capability to provide information about the currently active row.
 */
CapabilityStart(SingleRowInfo, EContextCapabilityFlags::SupportsSingle)
	/*
	 * Returns whether a column matches the requested type or not. This version only applies to current row. This version is faster than
	 * querying for an arbitrary row.
	 */
	ConstFunction1(SingleRowInfo, bool, CurrentRowHasColumns, (TConstArrayView<const UScriptStruct*>, Columns))
	/** Returns the currently active row. */
	ConstFunction0(SingleRowInfo, RowHandle, GetCurrentRow)

#if defined(WithWrappers)
	/*
	 * Returns whether a column matches the requested type or not. This version only applies to current row. This version is faster than
	 * querying for an arbitrary row.
	 */
	template<TColumnType... ColumnTypes>
	bool CurrentRowHasColumns() const
	{
		return this->CurrentRowHasColumns(TConstArrayView<const UScriptStruct*>({ ColumnTypes::StaticStruct()... }));
	}
#endif
CapabilityEnd(SingleRowInfo)

/**
 * Capability to provide information about the currently active batch.
 */
CapabilityStart(RowBatchInfo, EContextCapabilityFlags::SupportsBatch)
	/** Returns the number of rows in the current batch. */
	ConstFunction0(RowBatchInfo, uint32, GetBatchRowCount)
	/** Returns an view with the rows used by this batch. */
	ConstFunction0(RowBatchInfo, TConstArrayView<RowHandle>, GetBatchRowHandles)
	/** Checks if the rows in the current batch have the requested columns. */
	ConstFunction1(RowBatchInfo, bool, CurrentBatchTableHasColumns, (TConstArrayView<const UScriptStruct*>, Columns))

#if defined(WithWrappers)
	/** Checks if the rows in the current batch have the requested columns. */
	template<TColumnType... ColumnTypes>
	bool CurrentBatchTableHasColumns() const
	{
		return this->CurrentBatchTableHasColumns(TConstArrayView<const UScriptStruct*>({ ColumnTypes::StaticStruct()... }));
	}

	template<typename CallbackType, typename... Args>
	void ForEachRow(CallbackType&& Callback, TBatch<Args>... Batches)
	{
		uint32 RowCount = this->GetBatchRowCount();
		TTuple<Args*...> AddressTuple = MakeTuple(Batches.GetData()...);
		for (uint32 Index = 0; Index < RowCount; ++Index)
		{
			AddressTuple.ApplyBefore([&](Args*&... Pointers)
				{
					Callback((*Pointers++)...);
				});
		}
	}
#endif
CapabilityEnd(RowBatchInfo)