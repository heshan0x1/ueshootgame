// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcherBackend.h"
#include "IO/OnDemandError.h"
#include "Templates/SharedPointer.h"
#include "Containers/StringFwd.h"

struct FIoContainerHeader; 

namespace UE::IoStore
{

class FOnDemandContentHandle;
class FOnDemandIoStore;
struct FOnDemandInstallCacheUsage;
using FSharedOnDemandContainer = TSharedPtr<struct FOnDemandContainer, ESPMode::ThreadSafe>;

struct FOnDemandInstallCacheStorageUsage
{
	uint64 MaxSize = 0;
	uint64 TotalSize = 0;
	uint64 ReferencedBlockSize = 0;
};

class IOnDemandInstallCache 
	: public IIoDispatcherBackend
{
public:
	virtual										~IOnDemandInstallCache() = default;
	virtual bool								IsChunkCached(const FIoHash& ChunkHash) = 0;
	virtual bool								TryPinChunks(
													const FSharedOnDemandContainer& Container,
													TConstArrayView<int32> EntryIndices,
													FOnDemandContentHandle ContentHandle,
													TArray<int32>& OutMissing) = 0;
	virtual FResult								PutChunk(FIoBuffer&& Chunk, const FIoHash& ChunkHash) = 0;
	virtual FResult								Purge(uint64 BytesToInstall) = 0;
	virtual FResult								PurgeAllUnreferenced(bool bDefrag, const uint64* BytesToPurge = nullptr) = 0;
	virtual FResult								DefragAll(const uint64* BytesToFree = nullptr) = 0;
	virtual FResult								Verify() = 0;
	virtual FResult								Flush() = 0;
	virtual FResult								FlushLastAccess() = 0;
	virtual void								UpdateLastAccess(TConstArrayView<FIoHash> ChunkHashes) = 0;
	virtual FOnDemandInstallCacheUsage			GetCacheUsage() = 0;
};

struct FOnDemandInstallCacheConfig
{
	FString RootDirectory;
	uint64	DiskQuota		= 1ull << 30;
	uint64	JournalMaxSize	= 2ull << 20;
	double	LastAccessGranularitySeconds = 60 * 60;
	bool	bDropCache		= false;
};

TSharedPtr<IOnDemandInstallCache> MakeOnDemandInstallCache(
	FOnDemandIoStore& IoStore,
	const FOnDemandInstallCacheConfig& Config,
	class FDiskCacheGovernor& Governor);

} // namespace UE::IoStore
