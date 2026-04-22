// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMScope.h"

#include <type_traits>

namespace Verse
{
inline size_t VScope::NumBytes(uint32 NumCaptures)
{
	static_assert(std::is_array_v<decltype(Captures)>);
	return offsetof(VScope, Captures) + sizeof(std::remove_extent_t<decltype(Captures)>) * NumCaptures;
}

inline VScope& VScope::New(FAllocationContext Context, VScope* ParentScope, uint32 NumCaptures)
{
	return *new (Context.AllocateFastCell(NumBytes(NumCaptures))) VScope{Context, ParentScope, NumCaptures};
}

inline VScope& VScope::New(FAllocationContext Context, VClass* SuperClass)
{
	if (!SuperClass)
	{
		return New(Context);
	}
	VScope& Scope = New(Context, nullptr, 1);
	Scope.Captures[0].Set(Context, *SuperClass);
	return Scope;
}

inline VScope& VScope::New(FAllocationContext Context)
{
	return New(Context, nullptr, 0);
}

inline VScope& VScope::NewUninitialized(FAllocationContext Context, uint32 NumCaptures)
{
	return *new (Context.AllocateFastCell(NumBytes(NumCaptures))) VScope{Context, nullptr, NumCaptures};
}

inline VScope& VScope::GetRootScope()
{
	for (auto I = this;;)
	{
		auto J = I->ParentScope.Get();
		if (!J)
		{
			return *I;
		}
		I = J;
	}
}

inline VScope::VScope(FAllocationContext Context, VScope* ParentScope, uint32 NumCaptures)
	: VCell{Context, &GlobalTrivialEmergentType.Get(Context)}
	, ParentScope{Context, ParentScope}
	, NumCaptures{NumCaptures}
{
	for (auto I = Captures, Last = Captures + NumCaptures; I != Last; ++I)
	{
		static_assert(std::is_array_v<decltype(Captures)>);
		new (I) std::remove_extent_t<decltype(Captures)>{};
	}
}
} // namespace Verse

#endif // WITH_VERSE_VM
