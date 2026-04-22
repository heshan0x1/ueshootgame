// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGManagedFastGeoContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGManagedFastGeoContainer)

bool UPCGManagedFastGeoContainer::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGManagedFastGeoContainer::Release);

	if (FastGeo)
	{
		FastGeo->Unregister();

		// Note: This may be costly. Monitor performance and possibly build a way to keep the FastGeo container alive until async work is done.
		FastGeo->Tick(/*bWaitForCompletion=*/true);

		FastGeo = nullptr;
	}

	// Can always remove from PCG Component.
	return true;
}
