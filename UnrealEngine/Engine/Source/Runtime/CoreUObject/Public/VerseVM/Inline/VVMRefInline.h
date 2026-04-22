// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Templates/TypeHash.h"

#include "VerseVM/VVMRef.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMTask.h"
#include "VerseVM/VVMTransaction.h"

namespace Verse
{
inline void VRestValue::SetTransactionally(FAllocationContext Context, VValue NewValue)
{
	checkSlow(!NewValue.IsRoot());
	Value.SetTransactionally(Context, NewValue);
}

inline void VRestValue::SetTrailed(FAllocationContext Context, VValue NewValue)
{
	checkSlow(!NewValue.IsRoot());
	Value.SetTrailed(Context, NewValue);
}

template <typename FunctionType>
bool FRefAwaiterHeader::AnyOf(const TSet<FRefAwaiter>& Set, FunctionType F) const
{
	TArray<FSetElementId> ElementIds;
	ElementIds.Reserve(Set.Num());
	for (FSetElementId I = Next; I.IsValidId(); I = Set.Get(I).Next)
	{
		ElementIds.Add(I);
	}
	for (FSetElementId ElementId : ElementIds)
	{
		if (F(Set[ElementId]))
		{
			return true;
		}
	}
	return false;
}

inline bool operator==(const FRefAwaiter& Left, const FRefAwaiter& Right)
{
	return Left.Task == Right.Task && Left.AwaitPC == Right.AwaitPC;
}

inline uint32 GetTypeHash(const FRefAwaiter& Arg)
{
	uint32 Result = 0;
	Result = HashCombineFast(Result, GetTypeHash(Arg.Task));
	Result = HashCombineFast(Result, PointerHash(Arg.AwaitPC));
	return Result;
}

template <typename UnaryFunction>
inline bool VRefRareData::AnyAwaitTask(UnaryFunction F) const
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");
	return AwaiterHeader.AnyOf(AwaiterBuffer, [&](const FRefAwaiter& Awaiter) {
		AutoRTFM::UnreachableIfClosed("#jira SOL-8415");
		if (!ContainsAwaitTask(*Awaiter.Task, *Awaiter.AwaitPC))
		{
			return false;
		}
		return F(*Awaiter.Task);
	});
}

inline void VRef::Set(FAllocationContext Context, VValue NewValue)
{
	return Value.SetTransactionally(Context, NewValue);
}
} // namespace Verse
#endif // WITH_VERSE_VM
