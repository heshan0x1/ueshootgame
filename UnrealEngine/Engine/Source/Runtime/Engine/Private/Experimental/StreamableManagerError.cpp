// Copyright Epic Games, Inc. All Rights Reserved.
#include "Engine/Experimental/StreamableManagerError.h"
#include "UObject/UObjectGlobals.h"

UE_DEFINE_ERROR_MODULE(StreamableManager);

UE_DEFINE_ERROR( PackageLoadFailed, StreamableManager);
UE_DEFINE_ERROR(PackageLoadCanceled, StreamableManager);
UE_DEFINE_ERROR(DownloadError, StreamableManager);
UE_DEFINE_ERROR(PackageNameInvalid, StreamableManager);
UE_DEFINE_ERROR(IoStoreNotFound, StreamableManager);
UE_DEFINE_ERROR(SyncLoadIncomplete, StreamableManager);
UE_DEFINE_ERROR(AsyncLoadFailed, StreamableManager);
UE_DEFINE_ERROR(AsyncLoadCancelled, StreamableManager);
UE_DEFINE_ERROR(AsyncLoadUnknownError, StreamableManager);
UE_DEFINE_ERROR(UnknownError, StreamableManager);
UE_DEFINE_ERROR(AsyncLoadNotInstalled, StreamableManager);

namespace UE::UnifiedError::StreamableManager
{

UE::UnifiedError::FError GetStreamableError(EAsyncLoadingResult::Type Result)
{
	switch (Result)
	{
	case EAsyncLoadingResult::Failed:
	case EAsyncLoadingResult::FailedMissing:
	case EAsyncLoadingResult::FailedLinker:
		// We could supply an error string if async loading bubbled one up...
		// Possibly GetExplanationForUnavailablePackage?
		return AsyncLoadFailed::MakeError();
	case EAsyncLoadingResult::FailedNotInstalled:
		return AsyncLoadNotInstalled::MakeError();
	case EAsyncLoadingResult::Canceled:
		return AsyncLoadCancelled::MakeError();
	}

	return AsyncLoadUnknownError::MakeError((int32)Result);
}

} // namespace UE::UnifiedError::StreamableManager