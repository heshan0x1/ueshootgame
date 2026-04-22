// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMWeakCellMap.h"

namespace Verse
{

template <typename FunctorType>
void VWeakCellMap::ForEach(FunctorType&& F)
{
	FCellUniqueLock Lock(Mutex);
	for (const TPair<VCell*, VCell*>& Pair : Map)
	{
		F(Pair.Key, Pair.Value);
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
