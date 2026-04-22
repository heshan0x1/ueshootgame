// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Memory/SharedBuffer.h"

DECLARE_DELEGATE_OneParam(FOnFetchedCacheDataDelegate, FSharedBuffer);

namespace UE::MetaHuman
{
	// returns true if at least a DDC is available
	bool CacheAvailable();
	// tries to find the given key in the cache and returns its contents if found (blocking)
	[[nodiscard]] FSharedBuffer TryCacheFetch(const FString& KeyString);
	// tries to find the given key in the cache asynchronously. The delegate is invoked with a valid, or empty, FSharedBuffer instance
	void TryCacheFetchAsync(const FString& KeyString, FOnFetchedCacheDataDelegate&& OnFetchedCacheDataDelegate);
	// tries to update the cache entry for the given key with the given content (can silently fail)
	void UpdateCacheAsync(const FString& KeyString, FSharedString InRequestName, FSharedBuffer InOutSharedBuffer);
}