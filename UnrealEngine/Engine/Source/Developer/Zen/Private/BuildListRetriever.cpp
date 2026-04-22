// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/BuildListRetriever.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"

namespace UE::Zen::Build
{

FBuildListRetriever::FBuildListRetriever()
{
	ServiceInstance = MakeShared<FBuildServiceInstance>();
}

void FBuildListRetriever::ConnectToBuildService()
{
	if (bConnected)
		return;

	bConnected = true;
	ServiceInstance->OnRefreshNamespacesAndBucketsComplete().AddSP(this, &FBuildListRetriever::OnNamespacesAndBucketsRefreshed);
	ServiceInstance->Connect(!FApp::IsUnattended(), [this]
		(UE::Zen::Build::FBuildServiceInstance::EConnectionState ConnectionState,
			UE::Zen::Build::FBuildServiceInstance::EConnectionFailureReason FailureReason)
		{
			if (ConnectionState == UE::Zen::Build::FBuildServiceInstance::EConnectionState::ConnectionSucceeded)
			{
				ServiceInstance->RefreshNamespacesAndBuckets();
			}
		});
}

void FBuildListRetriever::OnNamespacesAndBucketsRefreshed()
{
	ExecuteOnGameThread(UE_SOURCE_LOCATION,
		[this]
		{
			ReadyToIssueBuildQuery = true;
		});
}

bool FBuildListRetriever::QueryBuilds(const FString& InProjectName, const FString& InBuildType, const FString& InStream, FOnQueriesComplete&& InOnQueriesCompleteCallback)
{
	check(IsInGameThread());
	if (ReadyToIssueBuildQuery)
	{
		ProjectName = InProjectName;
		BuildType = InBuildType;
		Stream = InStream;
		OnQueriesCompleteCallback = MoveTemp(InOnQueriesCompleteCallback);

		IssueBuildQueries();
		return true;
	}
	else
	{
		// Don't do anything if an existing query is in flight
		return false;
	}
}

void FBuildListRetriever::IssueBuildQueries()
{
	check(IsInGameThread());

	TMultiMap<FString, FString> NamespacesAndBuckets = ServiceInstance->GetNamespacesAndBuckets();
	TMultiMap<FString, FString> BucketsToNamespaces;
	for (const TPair<FString, FString>& NamespaceAndBucket : NamespacesAndBuckets)
	{
		BucketsToNamespaces.AddUnique(NamespaceAndBucket.Value, NamespaceAndBucket.Key);
	}
	
	auto StringToSegmentViews = [](const FString& Str, TArray<FStringView>& OutViews)
		{
			FStringView WorkingStringView(Str);
			int32 CurrentIndex = 0;
			while (WorkingStringView.FindChar(TCHAR('.'), CurrentIndex))
			{
				if (CurrentIndex != 0)
				{
					OutViews.Add(WorkingStringView.Left(CurrentIndex));
				}
				WorkingStringView.RightChopInline(CurrentIndex + 1);
			}
			if (!WorkingStringView.IsEmpty())
			{
				OutViews.Add(WorkingStringView);
			}
		};

	TMultiMap<FStringView, TArray<FStringView>> NamespacesToBucketSegmentViews;
	for (const TPair<FString, FString>& NamespaceAndBucket : NamespacesAndBuckets)
	{
		BucketsToNamespaces.AddUnique(NamespaceAndBucket.Value, NamespaceAndBucket.Key);
		TArray<FStringView>& BucketSegmentViews = NamespacesToBucketSegmentViews.Add(NamespaceAndBucket.Key);
		StringToSegmentViews(NamespaceAndBucket.Value, BucketSegmentViews);
	}

	const uint32 SegmentIndexProject = 0;
	const uint32 SegmentIndexBuildType = 1;
	const uint32 SegmentIndexStream = 2;
	const uint32 SegmentIndexPlatform = 3;
	const uint32 SegmentIndexNum = 4;

	// Platform list generation
	TArray<FString> Platforms;
	for (const TPair<FStringView, TArray<FStringView>>& NamespaceToBucketSegmentViews : NamespacesToBucketSegmentViews)
	{
		const TArray<FStringView>& BucketSegmentViews = NamespaceToBucketSegmentViews.Value;
		if (BucketSegmentViews.Num() == SegmentIndexNum)
		{
			if (BucketSegmentViews[SegmentIndexStream] != Stream)
			{
				continue;
			}

			if (BucketSegmentViews[SegmentIndexProject] != ProjectName)
			{
				continue;
			}

			if (BucketSegmentViews[SegmentIndexBuildType] != BuildType)
			{
				continue;
			}

			Platforms.AddUnique(FString(BucketSegmentViews[SegmentIndexPlatform]));
		}
	}

	TArray<FNamespacePlatformBucketTuple> NamespacePlatformBucketTuples;
	for (const FString& Platform : Platforms)
	{
		FString Bucket = FString::Printf(TEXT("%s.%s.%s.%s"), *ProjectName, *BuildType, *Stream, *Platform);
		// TODO: replace with passed in namespace
		TArray<FString> NamespacesForBucket;
		BucketsToNamespaces.MultiFind(Bucket, NamespacesForBucket);
		for (FString& Namespace : NamespacesForBucket)
		{
			NamespacePlatformBucketTuples.Emplace(MoveTemp(Namespace), *Platform, Bucket);
		}
	}

	if (NamespacePlatformBucketTuples.Num() > 0)
	{
		ReadyToIssueBuildQuery = false;
		
		TSharedPtr<FListBuildsState> PendingQueryState = MakeShared<FListBuildsState>();
		PendingQueryState->PendingQueries = NamespacePlatformBucketTuples.Num();
		PendingQueryState->QueryState.SetNum(NamespacePlatformBucketTuples.Num());
		uint32 QueryIndex = 0;

		for (const FNamespacePlatformBucketTuple& NamespacePlatformBucket : NamespacePlatformBucketTuples)
		{
			ServiceInstance->ListBuilds(NamespacePlatformBucket.Namespace, NamespacePlatformBucket.Bucket,
				[this, QueryIndex, Namespace = NamespacePlatformBucket.Namespace, Platform = NamespacePlatformBucket.Platform,
				PendingQueryState]
				(TArray<FBuildServiceInstance::FBuildRecord>&& Results) mutable
				{
					FBuildState& NewBuildState = PendingQueryState->QueryState[QueryIndex];
					NewBuildState.Namespace = MoveTemp(Namespace);
					NewBuildState.Platform = MoveTemp(Platform);
					NewBuildState.Results = MoveTemp(Results);

					if (--PendingQueryState->PendingQueries == 0)
					{
						// All queries complete
						ExtractBuildsFromQueryResult(*PendingQueryState);

						ExecuteOnGameThread(UE_SOURCE_LOCATION,
							[this]
							{
								if (OnQueriesCompleteCallback)
								{
									OnQueriesCompleteCallback(PerPlatformBuilds);
								}
								ReadyToIssueBuildQuery = true;
							});
					}
				});

			++QueryIndex;
		}
	}
}

void FBuildListRetriever::ExtractBuildsFromQueryResult(FListBuildsState& ListBuildsState)
{
	PerPlatformBuilds.Reset();
	for (FBuildState& BuildState : ListBuildsState.QueryState)
	{
		for (FBuildServiceInstance::FBuildRecord& BuildRecord : BuildState.Results)
		{
			TArray<int32>& Builds = PerPlatformBuilds.FindOrAdd(BuildState.Platform);
			FString CommitIdentifier = BuildRecord.GetCommitIdentifier();
			Builds.Add(FCString::Atoi(*CommitIdentifier));
		}
	}

	// Sort builds in ascending order for each platform
	for (auto& Pair : PerPlatformBuilds)
	{
		Pair.Value.Sort(TLess<int32>());
	}
}

} // namespace UE::Zen::Build
