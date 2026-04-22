// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMPlaceholder.h"
#include "VerseVM/VVMTransaction.h"

namespace Verse
{
inline void VPlaceholder::SetModeTrailed(FAllocationContext Context, EMode InMode)
{
	if (FTrail* Trail = Context.CurrentTrail())
	{
		Trail->LogBeforeWrite(Context, Mode());
	}
	Mode() = InMode;
}
} // namespace Verse

#endif
