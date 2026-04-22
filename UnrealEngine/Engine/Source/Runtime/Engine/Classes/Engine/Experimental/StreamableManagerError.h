// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Experimental/UnifiedError/UnifiedError.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPath.h"


UE_DECLARE_ERROR_MODULE(ENGINE_API, StreamableManager);

UE_DECLARE_ERROR_ONEPARAM(	ENGINE_API, PackageLoadFailed, 1, StreamableManager, NSLOCTEXT("StreamableManager", "PackageLoadError", "Failed to load package {PackageName}"), FString, PackageName, TEXT("Unknown"));
UE_DECLARE_ERROR_ONEPARAM(	ENGINE_API, PackageLoadCanceled, 2, StreamableManager, NSLOCTEXT("StreamableManager", "PackageLoadCancelled", "Async load canceled {PackageName}"), FString, PackageName, TEXT("Unknown"));
UE_DECLARE_ERROR(			ENGINE_API, DownloadError, 3, StreamableManager, NSLOCTEXT("StreamableManager", "DownloadError", "Failed to download"));
UE_DECLARE_ERROR_ONEPARAM(	ENGINE_API, PackageNameInvalid, 4, StreamableManager, NSLOCTEXT("StreamableManager", "PackageNameInvalid", "Found invalid package name {InvalidPackageName}"), FString, InvalidPackageName, TEXT("Unknown"));

UE_DECLARE_ERROR(			ENGINE_API, IoStoreNotFound, 6, StreamableManager, NSLOCTEXT("StreamableManager", "IoStoreNotFound", "IoStore did not load correctly."));
UE_DECLARE_ERROR_ONEPARAM(	ENGINE_API, SyncLoadIncomplete, 7, StreamableManager, NSLOCTEXT("StreamableManager", "SyncLoadIncomplete", "Sync load did not complete correctly for {DebugName}."), FString, DebugName, TEXT("Unknown"));

UE_DECLARE_ERROR(			ENGINE_API, AsyncLoadFailed, 8, StreamableManager, NSLOCTEXT("StreamableManager", "AsyncLoadFailed", "Async load failed"));
UE_DECLARE_ERROR(			ENGINE_API, AsyncLoadCancelled, 9, StreamableManager, NSLOCTEXT("StreamableManager", "AsyncLoadCancelled", "Async load cancelled"));
UE_DECLARE_ERROR_ONEPARAM(	ENGINE_API, AsyncLoadUnknownError, 10, StreamableManager, NSLOCTEXT("StreamableManager", "AsyncLoadUnknownError", "Unknown async loading error {AsyncLoadingErrorId}."), int32, AsyncLoadingErrorId, -1);
UE_DECLARE_ERROR(			ENGINE_API, UnknownError, 11, StreamableManager, NSLOCTEXT("StreamableManager", "UnknownError", "Unknown error occured while streaming asset"));
UE_DECLARE_ERROR(			ENGINE_API, AsyncLoadNotInstalled, 12, StreamableManager, NSLOCTEXT("StreamableManager", "AsyncLoadNotInstalled", "Async load failed because the package is not installed."));

namespace UE::UnifiedError::StreamableManager
{
	UE::UnifiedError::FError GetStreamableError(EAsyncLoadingResult::Type Result);


	class FStreamableManagerAdditionalContext
	{
	public:
		FString RequestedPackageName;
		FString MissingObject;
	};

	class FStreamableManagerRequestContext
	{
	public:
		FString DebugName;
		TArray<FSoftObjectPath> RequestedAssets;
	};
	
}
UE_DECLARE_ERRORSTRUCT_FEATURES(StreamableManager, FStreamableManagerAdditionalContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::StreamableManager::FStreamableManagerAdditionalContext& Context)
{
	Writer.BeginObject();
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::StreamableManager::FStreamableManagerAdditionalContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("(RequstedPackage: {RequestedPackageName}, MissingObject: {MissingObject})"));
	Writer.AddString(ANSITEXTVIEW("RequestedPackageName"), Context.RequestedPackageName);
	Writer.AddString(ANSITEXTVIEW("MissingObject"), Context.MissingObject);
	Writer.EndObject();
}

UE_DECLARE_ERRORSTRUCT_FEATURES(StreamableManager, FStreamableManagerRequestContext);
inline void SerializeForLog(FCbWriter& Writer, const UE::UnifiedError::StreamableManager::FStreamableManagerRequestContext& Context)
{
	Writer.BeginObject(); 
	Writer.AddString(ANSITEXTVIEW("$type"), TErrorStructFeatures<UE::UnifiedError::StreamableManager::FStreamableManagerRequestContext>::GetErrorContextTypeNameAsString());
	Writer.AddString(ANSITEXTVIEW("$format"), TEXT("(RequestDebugName: {DebugName}, RequestedAssets: {RequestedAssets})"));
	Writer.AddString(ANSITEXTVIEW("DebugName"), Context.DebugName);
	Writer.BeginArray(ANSITEXTVIEW("RequestedAssets"));
	for (const FSoftObjectPath& RequestedAsset : Context.RequestedAssets )
	{
		SerializeForLog(Writer, RequestedAsset);
	}
	Writer.EndArray();

	Writer.EndObject();
}