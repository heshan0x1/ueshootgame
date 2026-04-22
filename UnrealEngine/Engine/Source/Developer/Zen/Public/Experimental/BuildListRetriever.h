// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Experimental/BuildServerInterface.h"
#include "ZenBuildUtils.h"

#define UE_API ZEN_API

namespace UE::Zen::Build
{
	/**
	 * Helper class to query a list of builds for a project of a specified build type in a P4 stream.
	 * Call ConnectToBuildService() first to establish connection, before calling QueryBuilds() to retrieve the builds, build query is asynchronous.
	 */
	class FBuildListRetriever : public TSharedFromThis<FBuildListRetriever>
	{
	public:
		UE_API FBuildListRetriever();
		typedef TMap<FString, TArray<int32>> FPlatformBuildsMap;
		typedef TUniqueFunction<void(const FPlatformBuildsMap&)> FOnQueriesComplete;
		UE_API void ConnectToBuildService();
		UE_API bool QueryBuilds(const FString& InProjectName, const FString& InBuildType, const FString& InStream, FOnQueriesComplete&& InOnQueriesCompleteCallback);

	private:
		void OnNamespacesAndBucketsRefreshed();
		void IssueBuildQueries();
		void ExtractBuildsFromQueryResult(FListBuildsState& ListBuildsState);

		FString ProjectName;
		FString BuildType;
		FString Stream;

		mutable TSharedPtr<FBuildServiceInstance> ServiceInstance;
		FPlatformBuildsMap PerPlatformBuilds;
		FOnQueriesComplete OnQueriesCompleteCallback;
		bool ReadyToIssueBuildQuery = false; // Should only be accessed from the game thread
		bool bConnected = false;
	};

} // namespace UE::Zen::Build

#undef UE_API
