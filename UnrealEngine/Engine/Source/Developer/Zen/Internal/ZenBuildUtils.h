// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Experimental/BuildServerInterface.h"

namespace UE::Zen::Build
{
	struct FNamespacePlatformBucketTuple
	{
		FString Namespace;
		FString Platform;
		FString Bucket;
	};

	struct FBuildState
	{
		FString Namespace;
		FString Platform;
		TArray<UE::Zen::Build::FBuildServiceInstance::FBuildRecord> Results;
	};

	struct FListBuildsState
	{
		std::atomic<uint32> PendingQueries;
		TArray<FBuildState> QueryState;
	};
} // namespace UE::Zen::Build

