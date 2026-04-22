// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMBytecode.h"
#include "VerseVM/VVMBytecodeAnalysis.h"

namespace Verse
{

// Mapping from register index to name. VProcedures hold an array of such mappings.
struct FRegisterName
{
	FRegisterIndex Index;
	TWriteBarrier<VUniqueString> Name;
	BytecodeAnalysis::FLiveRange LiveRange;

	FRegisterName() = default;

	FRegisterName(FAccessContext InContext, FRegisterIndex InIndex, VUniqueString& InName)
		: Index(InIndex)
		, Name(InContext, InName)
	{
	}

	friend bool operator<(const FRegisterName& Left, const FRegisterName& Right)
	{
		if (Left.Index < Right.Index)
		{
			return true;
		}
		if (Right.Index < Left.Index)
		{
			return false;
		}
		if (Left.LiveRange < Right.LiveRange)
		{
			return true;
		}
		if (Right.LiveRange < Left.LiveRange)
		{
			return false;
		}
		// Nondeterministic, so do last.
		return Left.Name.Get() < Right.Name.Get();
	}

	template <typename TVisitor>
	friend void Visit(TVisitor& Visitor, FRegisterName& Value)
	{
		Visitor.Visit(Value.Index, TEXT("Index"));
		Visitor.Visit(Value.Name, TEXT("Name"));
		Visitor.Visit(Value.LiveRange, TEXT("LiveRange"));
	}
};

} // namespace Verse

#endif
