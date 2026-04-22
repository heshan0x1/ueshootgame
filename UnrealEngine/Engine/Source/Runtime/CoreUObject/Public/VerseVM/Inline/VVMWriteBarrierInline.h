// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMTransaction.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{
template <typename T>
inline void TWriteBarrier<T>::ResetTransactionally(FAllocationContext Context)
{
	FTransaction* Transaction = Context.CurrentTransaction();
	checkSlow(Transaction);
	Transaction->LogBeforeWrite(Context, *this);
	Reset();
}

template <typename T>
inline void TWriteBarrier<T>::SetTransactionally(FAllocationContext Context, TValue NewValue)
{
	FTransaction* Transaction = Context.CurrentTransaction();
	checkSlow(Transaction);
	Transaction->LogBeforeWrite(Context, *this);
	Set(Context, NewValue);
}

template <typename T>
template <typename TResult>
auto TWriteBarrier<T>::SetTransactionally(FAllocationContext Context, T& NewValue) -> std::enable_if_t<!bIsVValue, TResult>
{
	SetTransactionally(Context, &NewValue);
}

template <typename T>
template <typename TResult>
auto TWriteBarrier<T>::SetNonCellNorPlaceholderTransactionally(FAllocationContext Context, VValue NewValue) -> std::enable_if_t<bIsVValue, TResult>
{
	FTransaction* Transaction = Context.CurrentTransaction();
	checkSlow(Transaction);
	Transaction->LogBeforeWrite(Context, *this);
	SetNonCellNorPlaceholder(NewValue);
}

template <typename T>
inline void TWriteBarrier<T>::SetTrailed(FAllocationContext Context, TValue NewValue)
{
	if (FTrail* Trail = Context.CurrentTrail())
	{
		Trail->LogBeforeWrite(Context, *this);
	}
	Set(Context, NewValue);
}

template <typename T>
template <typename TResult>
auto TWriteBarrier<T>::SetNonCellNorPlaceholderTrailed(FAllocationContext Context, VValue NewValue) -> std::enable_if_t<bIsVValue, TResult>
{
	if (FTrail* Trail = Context.CurrentTrail())
	{
		Trail->LogBeforeWrite(Context, *this);
	}
	SetNonCellNorPlaceholder(NewValue);
}
} // namespace Verse

#endif
