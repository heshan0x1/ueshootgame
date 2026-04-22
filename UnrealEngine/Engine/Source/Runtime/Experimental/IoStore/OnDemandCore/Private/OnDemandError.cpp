// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/OnDemandError.h"

UE_DEFINE_ERROR_MODULE(IoStoreOnDemand);
UE_DEFINE_ERROR(HttpError, IoStoreOnDemand);
UE_DEFINE_ERROR(ChunkMissingError, IoStoreOnDemand);
UE_DEFINE_ERROR(ChunkHashError, IoStoreOnDemand);
UE_DEFINE_ERROR(InstallCacheFlushError, IoStoreOnDemand);
UE_DEFINE_ERROR(InstallCacheFlushLastAccessError, IoStoreOnDemand);
UE_DEFINE_ERROR(InstallCachePurgeError, IoStoreOnDemand);
UE_DEFINE_ERROR(InstallCacheDefragError, IoStoreOnDemand);
UE_DEFINE_ERROR(InstallCacheVerificationError, IoStoreOnDemand);
UE_DEFINE_ERROR(CasError, IoStoreOnDemand);
UE_DEFINE_ERROR(CasJournalError, IoStoreOnDemand);
UE_DEFINE_ERROR(CasSnapshotError, IoStoreOnDemand);

namespace UE::UnifiedError::IoStore
{
} // namespace UE::UnifiedError::IoStore
