// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{
template <typename DerivedType>
struct TIntrusiveTree
{
	TWriteBarrier<DerivedType> Parent;
	TWriteBarrier<DerivedType> LastChild;
	TWriteBarrier<DerivedType> Prev;
	TWriteBarrier<DerivedType> Next;

	TIntrusiveTree(FAccessContext Context, DerivedType* Parent)
		: Parent(Context, Parent)
	{
		if (Parent)
		{
			if (Parent->LastChild)
			{
				Prev.Set(Context, Parent->LastChild.Get());
				Parent->LastChild->Next.Set(Context, This());
			}
			Parent->LastChild.Set(Context, This());
		}
	}

	template <bool bTransactional>
	void Detach(FAllocationContext Context)
	{
		if (Parent && Parent->LastChild.Get() == This())
		{
			V_DIE_IF(Next);
			Parent->LastChild.template Set<bTransactional>(Context, Prev.Get());
		}
		if (Prev)
		{
			V_DIE_UNLESS(Prev->Next.Get() == This());
			Prev->Next.template Set<bTransactional>(Context, Next.Get());
		}
		if (Next)
		{
			V_DIE_UNLESS(Next->Prev.Get() == This());
			Next->Prev.template Set<bTransactional>(Context, Prev.Get());
		}
		Prev.template Reset<bTransactional>(Context);
		Next.template Reset<bTransactional>(Context);
	}

	void Detach(FAllocationContext Context)
	{
		Detach<false>(::MoveTemp(Context));
	}

	void DetachTransactionally(FAllocationContext Context)
	{
		Detach<true>(::MoveTemp(Context));
	}

	// Visit each element of the subtree rooted at `this`.
	template <typename FunctionType>
	void ForEach(FunctionType&& Function)
	{
		if (LIKELY(!LastChild.Get()))
		{
			Function(*This());
			return;
		}

		TArray<DerivedType*> ToVisit;
		ToVisit.Push(This());
		while (ToVisit.Num())
		{
			DerivedType* Derived = ToVisit.Pop();
			Function(*Derived);
			for (DerivedType* Child = Derived->LastChild.Get(); Child; Child = Child->Prev.Get())
			{
				ToVisit.Push(Child);
			}
		}
	}

	DerivedType* This()
	{
		return static_cast<DerivedType*>(this);
	}

	template <typename TVisitor>
	void VisitReferencesImpl(TVisitor& Visitor)
	{
		Visitor.Visit(Parent, TEXT("Parent"));
		Visitor.Visit(LastChild, TEXT("LastChild"));
		Visitor.Visit(Prev, TEXT("Prev"));
		Visitor.Visit(Next, TEXT("Next"));
	}
};
} // namespace Verse

#endif
