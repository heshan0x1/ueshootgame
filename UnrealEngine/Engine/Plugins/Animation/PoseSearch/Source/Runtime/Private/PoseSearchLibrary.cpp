// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchLibrary.h"

#if UE_POSE_SEARCH_TRACE_ENABLED
#include "ObjectTrace.h"
#endif
#include "Animation/AnimationAsset.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimTrace.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/Trace/PoseSearchTraceLogger.h"
#include "PoseSearch/PoseSearchFeatureChannel_PermutationTime.h"
#include "UObject/FastReferenceCollector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchLibrary)

#define LOCTEXT_NAMESPACE "PoseSearchLibrary"

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
static bool GVarAnimMotionMatchDrawQueryEnable = false;
static FAutoConsoleVariableRef CVarAnimMotionMatchDrawQueryEnable(TEXT("a.MotionMatch.DrawQuery.Enable"), GVarAnimMotionMatchDrawQueryEnable, TEXT("Enable / Disable MotionMatch Draw Query"));

static bool GVarAnimMotionMatchDrawMatchEnable = false;
static FAutoConsoleVariableRef CVarAnimMotionMatchDrawMatchEnable(TEXT("a.MotionMatch.DrawMatch.Enable"), GVarAnimMotionMatchDrawMatchEnable, TEXT("Enable / Disable MotionMatch Draw Match"));
#endif

namespace UE::PoseSearch
{
	// an empty FStackAssetSet in any of the FAssetsToSearchPerDatabasePair entries means we need to search ALL the assets for the associated Database
	typedef TPair<const UPoseSearchDatabase*, FStackAssetSet> FAssetsToSearchPerDatabasePair;
	struct FAssetsToSearchPerDatabase : public TArray<FAssetsToSearchPerDatabasePair, TInlineAllocator<8, TMemStackAllocator<>>>
	{
		FAssetsToSearchPerDatabasePair* Find(const UPoseSearchDatabase* Database)
		{
			return FindByPredicate([Database](const FAssetsToSearchPerDatabasePair& Pair) { return Pair.Key == Database; });
		}

		bool Contains(const UPoseSearchDatabase* Database) const
		{
			return nullptr != FindByPredicate([Database](const FAssetsToSearchPerDatabasePair& Pair) { return Pair.Key == Database; });
		}
	};
	
	// used to cache the continuing pose search results
	struct FCachedContinuingPoseSearchResults : public TMap<const UObject*, FSearchResult, TInlineSetAllocator<16, TMemStackSetAllocator<>>>
	{
		const FSearchResult& CheckedAdd(const UObject* Object, const FSearchResult& SearchResult)
		{
			check(Object);
			check(!Find(Object));

			FSearchResult& NewSearchResult = Add(Object);
			NewSearchResult = SearchResult;
			return NewSearchResult;
		}

		const FSearchResult& FindOrAdd(const UObject* Object, const FSearchResult& SearchResult)
		{
			check(Object);
			if (const FSearchResult* FoundSearchResult = Find(Object))
			{
				return *FoundSearchResult;
			}

			FSearchResult& NewSearchResult = Add(Object);
			NewSearchResult = SearchResult;
			return NewSearchResult;
		}

		const FSearchResult& FindOrDefault(const UObject* Object) const
		{
			check(Object);
			if (const FSearchResult* FoundSearchResult = Find(Object))
			{
				return *FoundSearchResult;
			}

			static FSearchResult DefaultSearchResult;
			return DefaultSearchResult;
		}
	};

	// this function adds AssetToSearch to the search of Database
	// returns bAsyncBuildIndexInProgress
	static bool AddToSearchForDatabase(FAssetsToSearchPerDatabase& AssetsToSearchPerDatabase, const UObject* AssetToSearch, const UPoseSearchDatabase* Database, bool bContainsIsMandatory)
	{
		// making sure AssetToSearch is not a databases! later on we could add support for nested databases, but currently we don't support that
		check(Cast<const UPoseSearchDatabase>(AssetToSearch) == nullptr);

#if WITH_EDITOR
		// no need to check if Database is indexing if found into AssetsToSearchPerDatabase, since it already passed RequestAsyncBuildIndex successfully in a previous AddToSearchForDatabase call
		if (EAsyncBuildIndexResult::Success != FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			// database is still indexing... moving on
			return true;
		}
#endif // WITH_EDITOR

		if (!Database->Contains(AssetToSearch))
		{
			if (bContainsIsMandatory)
			{
				UE_LOG(LogPoseSearch, Error, TEXT("improperly setup UAnimSequenceBase. Database %s doesn't contain UAnimSequenceBase %s"), *Database->GetName(), *AssetToSearch->GetName());
			}
			return false;
		}

		if (FAssetsToSearchPerDatabasePair* AssetsToSearchPerDatabasePair = AssetsToSearchPerDatabase.Find(Database))
		{
			// an empty FStackAssetSet associated to Database means we need to search ALL the assets, so we don't need to add this AssetToSearch
			FStackAssetSet& AssetsToSearch = AssetsToSearchPerDatabasePair->Value;
			if (!AssetsToSearch.IsEmpty())
			{
				AssetsToSearch.Add(AssetToSearch);
			}
		}
		else
		{
			// no need to AddUnique since it's the first one
			FAssetsToSearchPerDatabasePair& NewAssetsToSearchPerDatabasePair = AssetsToSearchPerDatabase.AddDefaulted_GetRef();
			NewAssetsToSearchPerDatabasePair.Key = Database;
			NewAssetsToSearchPerDatabasePair.Value.Add(AssetToSearch);
		}

		return false;
	}

	// this function is looking for UPoseSearchDatabase(s) to search for the input AssetToSearch:
	// if AssetToSearch is a database search it ALL,
	// if it's a sequence containing UAnimNotifyState_PoseSearchBranchIn, we add to the search of the database UAnimNotifyState_PoseSearchBranchIn::Database the asset AssetToSearch
	// returns bAsyncBuildIndexInProgress
	static bool AddToSearch(FAssetsToSearchPerDatabase& AssetsToSearchPerDatabase, const UObject* AssetToSearch, bool bUsePoseSearchBranchIn)
	{
		if (bUsePoseSearchBranchIn)
		{
			if (const UAnimSequenceBase* SequenceBase = Cast<const UAnimSequenceBase>(AssetToSearch))
			{
				bool bAsyncBuildIndexInProgress = false;
				for (const FAnimNotifyEvent& NotifyEvent : SequenceBase->Notifies)
				{
					if (const UAnimNotifyState_PoseSearchBranchIn* PoseSearchBranchIn = Cast<UAnimNotifyState_PoseSearchBranchIn>(NotifyEvent.NotifyStateClass))
					{
						if (!PoseSearchBranchIn->Database)
						{
							UE_LOG(LogPoseSearch, Error, TEXT("improperly setup UAnimNotifyState_PoseSearchBranchIn with null Database in %s"), *SequenceBase->GetName());
							continue;
						}

						// we just skip indexing databases to keep the experience as smooth as possible
						if (AddToSearchForDatabase(AssetsToSearchPerDatabase, SequenceBase, PoseSearchBranchIn->Database, true))
						{
							bAsyncBuildIndexInProgress = true;
						}
					}
				}
				return bAsyncBuildIndexInProgress;
			}
		}

		if (const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(AssetToSearch))
		{
#if WITH_EDITOR
			if (EAsyncBuildIndexResult::Success != FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
			{
				return true;
			}
#endif // WITH_EDITOR

			// we already added Database to AssetsToSearchPerDatabase, so it already successfully passed RequestAsyncBuildIndex
			if (FAssetsToSearchPerDatabasePair* AssetsToSearchPerDatabasePair = AssetsToSearchPerDatabase.Find(Database))
			{
				// an empty FStackAssetSet associated to Database means we need to search ALL the assets
				FStackAssetSet& AssetsToSearch = AssetsToSearchPerDatabasePair->Value;
				AssetsToSearch.Reset();
			}
			else
			{
				// an empty FStackAssetSet associated to Database means we need to search ALL the assets
				AssetsToSearchPerDatabase.AddDefaulted_GetRef().Key = Database;
			}
			return false;
		}

		return false;
	}

	static void PopulateContinuingPoseSearches(const FPoseSearchContinuingProperties& ContinuingProperties, const TArrayView<const UObject*> AssetsToSearch, FSearchContext& SearchContext, FAssetsToSearchPerDatabase& ContinuingPoseAssetsToSearchPerDatabase, bool bUsePoseSearchBranchIn)
	{
		if (const UObject* PlayingAnimationAsset = ContinuingProperties.PlayingAsset.Get())
		{
			// checking if PlayingAnimationAsset can be considered or filtered out
			bool bCanBeConsidered = true;
			
			// if !AssetsToConsider or AssetsToConsider->IsEmpty(), we need to consider all the assets!
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (!SearchContext.GetInternalDeprecatedAssetsToConsider().IsEmpty())
			{
				// backward compatible path to support deprecated API FSearchContext::SetAssetsToConsider
				if (!SearchContext.GetInternalDeprecatedAssetsToConsider().Contains(PlayingAnimationAsset))
				{
					bCanBeConsidered = false;
				}
			}
			else
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			{
				if (const FStackAssetSet* AssetsToConsider = SearchContext.GetAssetsToConsiderSet())
				{
					if (!AssetsToConsider->IsEmpty() && !AssetsToConsider->Contains(PlayingAnimationAsset))
					{
						bCanBeConsidered = false;
					}
				}
			}

			if (bCanBeConsidered)
			{
				// checking if any of the AssetsToSearch (databases) or ContinuingProperties.PlayingAssetDatabase contain PlayingAnimationAsset
				if (ContinuingProperties.PlayingAssetDatabase)
				{
					if (AddToSearchForDatabase(ContinuingPoseAssetsToSearchPerDatabase, PlayingAnimationAsset, ContinuingProperties.PlayingAssetDatabase, false))
					{
#if WITH_EDITOR
						SearchContext.SetAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
					}
				}

				for (const UObject* AssetToSearch : AssetsToSearch)
				{
					if (const UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(AssetToSearch))
					{
						// since it cannot be a database we can directly add it to ContinuingPoseAssetsToSearchPerDatabase
						if (AddToSearchForDatabase(ContinuingPoseAssetsToSearchPerDatabase, PlayingAnimationAsset, Database, false))
						{
#if WITH_EDITOR
							SearchContext.SetAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
						}
					}
				}

				// checking if PlayingAnimationAsset has an associated database
				if (AddToSearch(ContinuingPoseAssetsToSearchPerDatabase, PlayingAnimationAsset, bUsePoseSearchBranchIn))
				{
#if WITH_EDITOR
					SearchContext.SetAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
				}
			}
		}
	}
	
	static void PopulateSearches(const TArrayView<const UObject*> AssetsToSearch, FSearchContext& SearchContext, FAssetsToSearchPerDatabase& AssetsToSearchPerDatabase, bool bUsePoseSearchBranchIn)
	{
		for (const UObject* AssetToSearch : AssetsToSearch)
		{
			if (AddToSearch(AssetsToSearchPerDatabase, AssetToSearch, bUsePoseSearchBranchIn))
			{
#if WITH_EDITOR
				SearchContext.SetAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
			}
		}

		// intersecting AssetsToSearchPerDatabase with asset to consider
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (!SearchContext.GetInternalDeprecatedAssetsToConsider().IsEmpty())
		{
			// backward compatible path to support deprecated API FSearchContext::SetAssetsToConsider
			for (FAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : AssetsToSearchPerDatabase)
			{
				const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
				check(Database);

				FStackAssetSet& AssetsToSearchForDatabase = AssetsToSearchPerDatabasePair.Value;
				if (!AssetsToSearchForDatabase.IsEmpty())
				{
					// doing an intersection between AssetsToSearchForDatabase and asset to consider
					FStackAssetSet AssetsToSearchForDatabaseIntersection;
					AssetsToSearchForDatabaseIntersection.Reserve(AssetsToSearchForDatabase.Num());
					for (const UObject* AssetToSearchForDatabase : AssetsToSearchForDatabase)
					{
						if (SearchContext.GetInternalDeprecatedAssetsToConsider().Contains(AssetToSearchForDatabase))
						{
							AssetsToSearchForDatabaseIntersection.Add(AssetToSearchForDatabase);
						}
					}
					AssetsToSearchForDatabase = AssetsToSearchForDatabaseIntersection;
				}
				else
				{
					// since all the database assets can be searched, we need to add only the one we can consider:
					for (const UObject* AssetToConsider : SearchContext.GetInternalDeprecatedAssetsToConsider())
					{
						if (Database->Contains(AssetToConsider))
						{
							AssetsToSearchForDatabase.Add(AssetToConsider);
						}
					}
				}
			}
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		else if (const FStackAssetSet* AssetsToConsider = SearchContext.GetAssetsToConsiderSet())
		{
			if (!AssetsToConsider->IsEmpty())
			{
				for (FAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : AssetsToSearchPerDatabase)
				{
					const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
					check(Database);

					FStackAssetSet& AssetsToSearchForDatabase = AssetsToSearchPerDatabasePair.Value;
					if (!AssetsToSearchForDatabase.IsEmpty())
					{
						// doing an intersection between AssetsToSearchForDatabase and asset to consider
						FStackAssetSet AssetsToSearchForDatabaseIntersection;
						AssetsToSearchForDatabaseIntersection.Reserve(AssetsToSearchForDatabase.Num());
						for (const UObject* AssetToSearchForDatabase : AssetsToSearchForDatabase)
						{
							if (AssetsToConsider->Contains(AssetToSearchForDatabase))
							{
								AssetsToSearchForDatabaseIntersection.Add(AssetToSearchForDatabase);
							}
						}
						AssetsToSearchForDatabase = AssetsToSearchForDatabaseIntersection;
					}
					else
					{
						// since all the database assets can be searched, we need to add only the one we can consider:
						// @todo: should we just do a copy? AssetsToSearchForDatabase = AssetsToConsider??
						for (const UObject* AssetToConsider : *AssetsToConsider)
						{
							if (Database->Contains(AssetToConsider))
							{
								AssetsToSearchForDatabase.Add(AssetToConsider);
							}
						}
					}
				}
			}
		}
	}

	// @todo: refine this logic. Currently if AssetsToSearch contains ONLY UPoseSearchDatabase we don't have to look for other databases referenced by other assets UAnimNotifyState_PoseSearchBranchIn(s)
	bool ShouldUsePoseSearchBranchIn(const TArrayView<const UObject*> AssetsToSearch)
	{
		for (const UObject* AssetToSearch : AssetsToSearch)
		{
			if (!Cast<UPoseSearchDatabase>(AssetToSearch))
			{
				return true;
			}
		}
		return false;
	}

	template <typename DatabasesContainer>
	static bool IsForceInterrupt(EPoseSearchInterruptMode InterruptMode, const UPoseSearchDatabase* CurrentResultDatabase, const DatabasesContainer& Databases)
	{
		switch (InterruptMode)
		{
		case EPoseSearchInterruptMode::DoNotInterrupt:
			return false;

		case EPoseSearchInterruptMode::InterruptOnDatabaseChange:	// Fall through
		case EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose:
			return !Databases.Contains(CurrentResultDatabase);

		case EPoseSearchInterruptMode::ForceInterrupt:				// Fall through
		case EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose:
			return true;

		default:
			checkNoEntry();
			return false;
		}
	}

	template <typename DatabasesContainer>
	static bool IsInvalidatingContinuingPose(EPoseSearchInterruptMode InterruptMode, const UPoseSearchDatabase* CurrentResultDatabase, const DatabasesContainer& Databases)
	{
		switch (InterruptMode)
		{
		case EPoseSearchInterruptMode::DoNotInterrupt:				// Fall through
		case EPoseSearchInterruptMode::InterruptOnDatabaseChange:	// Fall through
		case EPoseSearchInterruptMode::ForceInterrupt:	
			return false;

		case EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose:
			return !Databases.Contains(CurrentResultDatabase);

		case EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose:
			return true;

		default:
			checkNoEntry();
			return false;
		}
	}

	static bool ShouldUseCachedChannelData(const UPoseSearchDatabase* CurrentResultDatabase, const TConstArrayView<TObjectPtr<const UPoseSearchDatabase>> Databases)
	{
		const UPoseSearchSchema* OneOfTheSchemas = nullptr;
		if (CurrentResultDatabase)
		{
			OneOfTheSchemas = CurrentResultDatabase->Schema;
		}

		for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
		{
			if (Database)
			{
				if (OneOfTheSchemas != Database->Schema)
				{
					if (OneOfTheSchemas == nullptr)
					{
						OneOfTheSchemas = Database->Schema;
					}
					else
					{
						// we found we need to search multiple schemas
						return true;
					}
				}
			}
		}

		return false;
	}

	FRole GetCommonDefaultRole(const TConstArrayView<TObjectPtr<const UPoseSearchDatabase>> Databases)
	{
		FRole Role = DefaultRole;

		if (!Databases.IsEmpty())
		{
			if (const UPoseSearchDatabase* Database = Databases[0].Get())
			{
				if (const UPoseSearchSchema* Schema = Database->Schema)
				{
					Role = Schema->GetDefaultRole();
				}
			}

#if WITH_EDITOR && ENABLE_ANIM_DEBUG 
			for (int32 DatabaseIndex = 1; DatabaseIndex < Databases.Num(); ++DatabaseIndex)
			{
				if (const UPoseSearchDatabase* Database = Databases[DatabaseIndex].Get())
				{
					if (const UPoseSearchSchema* Schema = Database->Schema)
					{
						if (Role != Schema->GetDefaultRole())
						{
							UE_LOG(LogPoseSearch, Error, TEXT("GetCommonDefaultRole - inconsistent Role between provided Databases!"));
							break;
						}
					}
				}
			}
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG
		}

		return Role;
	}

	float CalculateWantedPlayRate(const FSearchResult& SearchResult, const FSearchContext& SearchContext, const FFloatInterval& PlayRate, float TrajectorySpeedMultiplier, const FPoseSearchEvent& EventToSearch)
	{
		float WantedPlayRate = 1.f;

		if (SearchResult.IsValid())
		{
			if (SearchResult.IsEventSearchResult())
			{
				// checking if SearchResult.EventPoseIdx is part of the EventToSearch.EventTag.
				// If not, it's an event from a continuing pose search that hasn't been interrupted,
				// so we keep the previously calculated WantedPlayRate
				const bool bIsEventSearchFromTag = SearchResult.IsEventSearchFromTag(EventToSearch.EventTag);
				if (bIsEventSearchFromTag)
				{
					const float TimeToEvent = SearchResult.CalculateTimeToEvent();
					if (TimeToEvent > UE_KINDA_SMALL_NUMBER && EventToSearch.TimeToEvent > UE_KINDA_SMALL_NUMBER)
					{
						// EventToSearch.TimeToEvent is the desired time to event, and TimeToEvent is the actually current time to event. we calculate WantedPlayRate as ratio between the two
						WantedPlayRate = TimeToEvent / EventToSearch.TimeToEvent;
					}
					// if we passed the event (TimeToEvent <= 0) we leave the WantedPlayRate as previously calculated
				}
			}
			else if (!ensure(PlayRate.Min <= PlayRate.Max && PlayRate.Min > UE_KINDA_SMALL_NUMBER))
			{
				UE_LOG(LogPoseSearch, Error, TEXT("Couldn't update the WantedPlayRate in CalculateWantedPlayRate, because of invalid PlayRate interval (%f, %f)"), PlayRate.Min, PlayRate.Max);
				WantedPlayRate = 1.f;
			}
			else if (!FMath::IsNearlyEqual(PlayRate.Min, PlayRate.Max, UE_KINDA_SMALL_NUMBER))
			{
				TConstArrayView<float> QueryData = SearchContext.GetCachedQuery(SearchResult.Database->Schema);
				if (!QueryData.IsEmpty())
				{
					if (const UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = SearchResult.Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())
					{
						const FSearchIndex& SearchIndex = SearchResult.Database->GetSearchIndex();
						const bool bReconstructPoseValues = SearchIndex.IsValuesEmpty();

						if (bReconstructPoseValues)
						{
							const int32 NumDimensions = SearchIndex.GetNumDimensions();

							// FMemory_Alloca is forced 16 bytes aligned and its allocated memory is in scope until the end of the function scope,
							// not the statement scope, so it's safe to use it in GetEstimatedSpeedRatio
							TArrayView<float> ReconstructedPoseValuesBuffer((float*)FMemory_Alloca(NumDimensions * sizeof(float)), NumDimensions);
							check(IsAligned(ReconstructedPoseValuesBuffer.GetData(), alignof(VectorRegister4Float)));

							const TConstArrayView<float> ResultData = SearchIndex.GetReconstructedPoseValues(SearchResult.PoseIdx, ReconstructedPoseValuesBuffer);
							if (ResultData.IsEmpty())
							{
								UE_LOG(LogPoseSearch, Warning,
									TEXT("Couldn't update the WantedPlayRate in CalculateWantedPlayRate, because couldn't reconstruct the pose value in GetReconstructedPoseValues for pose %d"),
									SearchResult.PoseIdx);
								WantedPlayRate = FMath::Clamp(1.f, PlayRate.Min, PlayRate.Max);
							}
							else
							{
								const float EstimatedSpeedRatio = TrajectoryChannel->GetEstimatedSpeedRatio(QueryData, ResultData);
								WantedPlayRate = FMath::Clamp(EstimatedSpeedRatio, PlayRate.Min, PlayRate.Max);
							}
						}
						else
						{
							const TConstArrayView<float> ResultData = SearchIndex.GetPoseValues(SearchResult.PoseIdx);
							const float EstimatedSpeedRatio = TrajectoryChannel->GetEstimatedSpeedRatio(QueryData, ResultData);
							WantedPlayRate = FMath::Clamp(EstimatedSpeedRatio, PlayRate.Min, PlayRate.Max);
						}
					}
					else
					{
						UE_LOG(LogPoseSearch, Warning,
							TEXT("Couldn't update the WantedPlayRate in CalculateWantedPlayRate, because Schema '%s' couldn't find a UPoseSearchFeatureChannel_Trajectory channel"),
							*GetNameSafe(SearchResult.Database->Schema));
					}
				}
			}
			else if (!FMath::IsNearlyZero(TrajectorySpeedMultiplier))
			{
				WantedPlayRate = PlayRate.Min / TrajectorySpeedMultiplier;
			}
			else
			{
				WantedPlayRate = PlayRate.Min;
			}
		}

		return WantedPlayRate;
	}
}

//////////////////////////////////////////////////////////////////////////
// FMotionMatchingState

void FMotionMatchingState::Reset(const FTransform& ComponentTransform)
{
	Reset();
}

void FMotionMatchingState::Reset()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bJumpedToPose = false;
	WantedPlayRate = 1.f;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	SearchResult = FPoseSearchBlueprintResult();
	// Set the elapsed time to INFINITY to trigger a search right away
	ElapsedPoseSearchTime = std::numeric_limits<float>::infinity();
}

void FMotionMatchingState::AdjustAssetTime(float AssetTime)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	CurrentSearchResult.UpdateWithNormalizedTime(AssetTime);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FVector FMotionMatchingState::GetEstimatedFutureRootMotionVelocity() const
{
	using namespace UE::PoseSearch;
	if (const UPoseSearchDatabase* Database = SearchResult.SelectedDatabase.Get())
	{
		if (const UPoseSearchFeatureChannel_Trajectory* TrajectoryChannel = Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>())
		{
			const int32 PoseIndex = Database->GetPoseIndex(SearchResult.SelectedAnim.Get(), SearchResult.SelectedTime, SearchResult.bIsMirrored, SearchResult.BlendParameters);
			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			if (!SearchIndex.IsValuesEmpty())
			{
				TConstArrayView<float> ResultData = SearchIndex.GetPoseValues(PoseIndex);
				return TrajectoryChannel->GetEstimatedFutureRootMotionVelocity(ResultData);
			}
		}
	}

	return FVector::ZeroVector;
}

void FMotionMatchingState::UpdateWantedPlayRate(const UE::PoseSearch::FSearchContext& SearchContext, const FFloatInterval& PlayRate, float TrajectorySpeedMultiplier, const FPoseSearchEvent& EventToSearch)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	WantedPlayRate = CalculateWantedPlayRate(CurrentSearchResult, SearchContext, PlayRate, TrajectorySpeedMultiplier, EventToSearch);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

//////////////////////////////////////////////////////////////////////////
// FPoseSearchContinuingProperties

void FPoseSearchContinuingProperties::InitFrom(const FPoseSearchBlueprintResult& SearchResult, EPoseSearchInterruptMode InInterruptMode)
{
	PlayingAsset = SearchResult.SelectedAnim;
	PlayingAssetAccumulatedTime = SearchResult.SelectedTime;
	bIsPlayingAssetMirrored = SearchResult.bIsMirrored;
	PlayingAssetBlendParameters = SearchResult.BlendParameters;
	InterruptMode = InInterruptMode;
	PlayingAssetDatabase = SearchResult.SelectedDatabase;
}

//////////////////////////////////////////////////////////////////////////
// UPoseSearchLibrary

#if UE_POSE_SEARCH_TRACE_ENABLED

void UPoseSearchLibrary::TraceMotionMatching(
	UE::PoseSearch::FSearchContext& SearchContext,
	const UE::PoseSearch::FSearchResult& SearchResult,
	float ElapsedPoseSearchTime,
	float DeltaTime,
	bool bSearch,
	float WantedPlayRate,
	EPoseSearchInterruptMode InterruptMode)
{
	using namespace UE::PoseSearch;
	FSearchResults_Single SearchResults;
	if (bSearch && SearchResult.IsValid())
	{
		SearchResults.UpdateWith(SearchResult);
	}
	TraceMotionMatching(SearchContext, SearchResults, ElapsedPoseSearchTime, WantedPlayRate, InterruptMode);
}

void UPoseSearchLibrary::TraceMotionMatching(
	UE::PoseSearch::FSearchContext& SearchContext,
	const UE::PoseSearch::FSearchResults& SearchResults,
	float ElapsedPoseSearchTime,
	float WantedPlayRate,
	EPoseSearchInterruptMode InterruptMode)
{
	using namespace UE::PoseSearch;

	const bool bChannelEnabled = UE_TRACE_CHANNELEXPR_IS_ENABLED(PoseSearchChannel);
	if (!bChannelEnabled)
	{
		return;
	}

	float RecordingTime = 0.f;
	if (!SearchContext.GetContexts().IsEmpty())
	{
		if (const UObject* FirstObject = SearchContext.GetContexts()[0]->GetFirstObjectParam())
		{
			RecordingTime = FObjectTrace::GetWorldElapsedTime(FirstObject->GetWorld());
		}
	}

	uint32 SearchId = 787;

	FTraceMotionMatchingStateMessage TraceState;

	TraceState.InterruptMode = InterruptMode;

	const int32 AnimContextsNum = SearchContext.GetContexts().Num();
	TraceState.SkeletalMeshComponentIds.SetNum(AnimContextsNum);

	for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < AnimContextsNum; ++AnimInstanceIndex)
	{
		if (const FChooserEvaluationContext* AnimContext = SearchContext.GetContexts()[AnimInstanceIndex])
		{
			const UObject* FirstObject = AnimContext->GetFirstObjectParam();
			const UObject* SkeletalMeshComponent = nullptr;
			if (const UAnimInstance* AnimInstance = Cast<UAnimInstance>(FirstObject))
			{
				SkeletalMeshComponent = AnimInstance->GetOuter();
			}
			else if (const UActorComponent* ActorComponent = Cast<UActorComponent>(FirstObject))
			{
				const AActor* Actor = ActorComponent->GetOwner();
				check(Actor);
				
				SkeletalMeshComponent = Actor->GetComponentByClass<USkeletalMeshComponent>();
			}

			if (!SkeletalMeshComponent || CANNOT_TRACE_OBJECT(SkeletalMeshComponent))
			{
				return;
			}
			
			TRACE_OBJECT(SkeletalMeshComponent);
			TraceState.SkeletalMeshComponentIds[AnimInstanceIndex] = FObjectTrace::GetObjectId(SkeletalMeshComponent);
		}
	}

	for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < AnimContextsNum; ++AnimInstanceIndex)
	{
		const FChooserEvaluationContext* Context = SearchContext.GetContexts()[AnimInstanceIndex];
		if (const UObject* Object = Context->GetFirstObjectParam())
		{
			TRACE_OBJECT(Object);
			SearchId = HashCombineFast(SearchId, GetTypeHash(FObjectTrace::GetObjectId(Object)));
		}
	}

	TraceState.Roles.SetNum(AnimContextsNum);
	for (const FRoleToIndexPair& RoleToIndexPair : SearchContext.GetRoleToIndex())
	{
		TraceState.Roles[RoleToIndexPair.Value] = RoleToIndexPair.Key;
	}

	SearchId = HashCombineFast(SearchId, GetTypeHash(TraceState.Roles));

	// @todo: do we need to hash pose history names in SearchId as well?
	TraceState.PoseHistories.SetNum(AnimContextsNum);
	for (int32 AnimInstanceIndex = 0; AnimInstanceIndex < AnimContextsNum; ++AnimInstanceIndex)
	{
		TraceState.PoseHistories[AnimInstanceIndex].InitFrom(SearchContext.GetPoseHistories()[AnimInstanceIndex]);
	}

	// finalizing SearchContext.GetBestPoseCandidatesMap() by calling SearchContext::Track for all the SearchResults flagging them as EPoseCandidateFlags::Valid_CurrentPose
	// because, those candidate could have been discarded becasue of their cost was too high and didn't fit into SearchContext.GetBestPoseCandidatesMap() limits
	SearchResults.IterateOverSearchResults([&SearchContext](const FSearchResult& SearchResult)
		{
			if (SearchResult.IsValid())
			{
				SearchContext.Track(SearchResult.Database.Get(), SearchResult.PoseIdx, EPoseCandidateFlags::Valid_CurrentPose, SearchResult.PoseCost);
			}
			return false;
		});

	TArray<uint64, TInlineAllocator<64>> DatabaseIds;
	int32 DbEntryIdx = 0;

	TraceState.DatabaseEntries.SetNum(SearchContext.GetBestPoseCandidatesMap().Num());
	for (TPair<const UPoseSearchDatabase*, FSearchContext::FBestPoseCandidates> DatabaseBestPoseCandidates : SearchContext.GetBestPoseCandidatesMap())
	{
		const UPoseSearchDatabase* Database = DatabaseBestPoseCandidates.Key;
		check(Database);

		FTraceMotionMatchingStateDatabaseEntry& DbEntry = TraceState.DatabaseEntries[DbEntryIdx];

		// if throttling is on, the continuing pose can be valid, but no actual search occurred, so the query will not be cached, and we need to build it
		DbEntry.QueryVector = SearchContext.GetOrBuildQuery(Database->Schema);
		TRACE_OBJECT(Database);
		DbEntry.DatabaseId = FObjectTrace::GetObjectId(Database);
		DatabaseIds.Add(DbEntry.DatabaseId);

		DatabaseBestPoseCandidates.Value.IterateOverBestPoseCandidates([&DbEntry](const FSearchContext::FPoseCandidate& PoseCandidate)
			{
				// @todo replace FTraceMotionMatchingStatePoseEntry with FSearchContext::FPoseCandidate
				FTraceMotionMatchingStatePoseEntry PoseEntry;
				PoseEntry.DbPoseIdx = PoseCandidate.PoseIdx;
				PoseEntry.Cost = PoseCandidate.Cost;
				PoseEntry.PoseCandidateFlags = PoseCandidate.PoseCandidateFlags;
				DbEntry.PoseEntries.Add(PoseEntry);
				return false;
			});

		++DbEntryIdx;
	}

	// @todo: reenable this code if needed
	//PRAGMA_DISABLE_DEPRECATION_WARNINGS
	//if (SearchResult.IsValid())
	//{
	//	TraceState.CurrentDbEntryIdx = DbEntryIdx;
	//	TraceState.CurrentPoseEntryIdx = DbEntry.PoseEntries.Add(PoseEntry);
	//}
	//PRAGMA_ENABLE_DEPRECATION_WARNINGS

	DatabaseIds.Sort();
	SearchId = HashCombineFast(SearchId, GetTypeHash(DatabaseIds));

	// @todo: instead of using the SearchResult (the best search result with the lowest cost) to calculate velocities etc, 
	//        shouldn't we aggregate the values from all the SearchResults (using SearchResults.IterateOverSearchResults)?
	const FSearchResult SearchResult = SearchResults.GetBestResult();
	const float AssetTime = SearchResult.IsValid() ? SearchResult.GetAssetTime() : 0.f;

	// @todo: integrate DeltaTime into SearchContext, and implement it for UAF as well
	float DeltaTime = FiniteDelta;
	if (!SearchContext.GetContexts().IsEmpty())
	{
		if (const UAnimInstance* AnimInstance = Cast<UAnimInstance>(SearchContext.GetContexts()[0]->GetFirstObjectParam()))
		{
			DeltaTime = AnimInstance->GetDeltaSeconds();
		}
	}

	if (DeltaTime > SMALL_NUMBER)
	{
		// simulation
		if (SearchContext.AnyCachedQuery())
		{
			TraceState.SimLinearVelocity = 0.f;
			TraceState.SimAngularVelocity = 0.f;

			const int32 NumRoles = SearchContext.GetRoleToIndex().Num();
			for (const FRoleToIndexPair& RoleToIndexPair : SearchContext.GetRoleToIndex())
			{
				const FRole& Role = RoleToIndexPair.Key;

				const FTransform PrevRoot = SearchContext.GetWorldBoneTransformAtTime(-DeltaTime, Role, RootSchemaBoneIdx);
				const FTransform CurrRoot = SearchContext.GetWorldBoneTransformAtTime(0.f, Role, RootSchemaBoneIdx);
				
				const FTransform SimDelta = CurrRoot.GetRelativeTransform(PrevRoot);
				TraceState.SimLinearVelocity += SimDelta.GetTranslation().Size() / (DeltaTime * NumRoles);
				TraceState.SimAngularVelocity += FMath::RadiansToDegrees(SimDelta.GetRotation().GetAngle()) / (DeltaTime * NumRoles);
			}
		}
		
		const FSearchIndexAsset* SearchIndexAsset = SearchResult.GetSearchIndexAsset();
		const UPoseSearchDatabase* CurrentResultDatabase = SearchResult.Database.Get();
		if (SearchIndexAsset && CurrentResultDatabase)
		{
			const FPoseSearchDatabaseAnimationAssetBase* DatabaseAsset = CurrentResultDatabase->GetDatabaseAnimationAsset(*SearchIndexAsset);
			check(DatabaseAsset);
			if (UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(DatabaseAsset->GetAnimationAsset()))
			{
				// Simulate the time step to get accurate root motion prediction for this frame.
				FAnimationAssetSampler Sampler(AnimationAsset, FTransform::Identity,FVector::ZeroVector, FAnimationAssetSampler::DefaultRootTransformSamplingRate, true, false);

				const float TimeStep = DeltaTime * WantedPlayRate;
				const FTransform PrevRoot = Sampler.ExtractRootTransform(AssetTime);
				const FTransform CurrRoot = Sampler.ExtractRootTransform(AssetTime + TimeStep);
				const FTransform RootMotionTransformDelta = PrevRoot.GetRelativeTransform(CurrRoot);
				TraceState.AnimLinearVelocity = RootMotionTransformDelta.GetTranslation().Size() / DeltaTime;
				TraceState.AnimAngularVelocity = FMath::RadiansToDegrees(RootMotionTransformDelta.GetRotation().GetAngle()) / DeltaTime;

				// Need another root motion extraction for non-playrate version in case acceleration isn't the same.
				const FTransform CurrRootNoTimescale = Sampler.ExtractRootTransform(AssetTime + DeltaTime);
				const FTransform RootMotionTransformDeltaNoTimescale = PrevRoot.GetRelativeTransform(CurrRootNoTimescale);
				TraceState.AnimLinearVelocityNoTimescale = RootMotionTransformDeltaNoTimescale.GetTranslation().Size() / DeltaTime;
				TraceState.AnimAngularVelocityNoTimescale = FMath::RadiansToDegrees(RootMotionTransformDeltaNoTimescale.GetRotation().GetAngle()) / DeltaTime;
			}
		}
		TraceState.Playrate = WantedPlayRate;
	}

	TraceState.ElapsedPoseSearchTime = ElapsedPoseSearchTime;
	TraceState.AssetPlayerTime = AssetTime;;
	TraceState.DeltaTime = DeltaTime;

	TraceState.RecordingTime = RecordingTime;
	TraceState.SearchBestCost = SearchResult.PoseCost;
#if WITH_EDITOR && ENABLE_ANIM_DEBUG
	TraceState.SearchBruteForceCost = SearchResult.BruteForcePoseCost;
	TraceState.SearchBestPosePos = SearchResult.BestPosePos;
#else // WITH_EDITOR && ENABLE_ANIM_DEBUG
	TraceState.SearchBruteForceCost = 0.f;
	TraceState.SearchBestPosePos = 0;
#endif // WITH_EDITOR && ENABLE_ANIM_DEBUG

	TraceState.Cycle = FPlatformTime::Cycles64();

	// @todo: avoid publishing duplicated TraceState in ALL the AnimContexts! -currently necessary for multi character-
	for (const FChooserEvaluationContext* Context : SearchContext.GetContexts())
	{
		const UObject* AnimContextObject = Context->GetFirstObjectParam();
		TRACE_OBJECT(AnimContextObject);
		TraceState.AnimInstanceId = FObjectTrace::GetObjectId(AnimContextObject);
		TraceState.NodeId = SearchId;
		TraceState.Output();
	}
}
#endif // UE_POSE_SEARCH_TRACE_ENABLED

void UPoseSearchLibrary::UpdateMotionMatchingState(
	const FAnimationUpdateContext& Context,
	const TArray<TObjectPtr<const UPoseSearchDatabase>>& Databases,
	float BlendTime,
	int32 MaxActiveBlends,
	const FFloatInterval& PoseJumpThresholdTime,
	float PoseReselectHistory,
	float SearchThrottleTime,
	const FFloatInterval& PlayRate,
	FMotionMatchingState& InOutMotionMatchingState,
	EPoseSearchInterruptMode InterruptMode,
	bool bShouldSearch,
	bool bShouldUseCachedChannelData,
	bool bDebugDrawQuery,
	bool bDebugDrawCurResult)
{
	using namespace UE::PoseSearch;

	if (Databases.IsEmpty())
	{
		Context.LogMessage(
			EMessageSeverity::Error,
			LOCTEXT("NoDatabases", "No database assets provided for motion matching."));
		return;
	}

	const IPoseHistory* PoseHistory = nullptr;
	if (const FPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<FPoseHistoryProvider>())
	{
		PoseHistory = &PoseHistoryProvider->GetPoseHistory();
	}

	check(Context.AnimInstanceProxy);
	FChooserEvaluationContext AnimContext(Context.AnimInstanceProxy->GetAnimInstanceObject());
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UpdateMotionMatchingState(&AnimContext, PoseHistory, Databases, Context.GetDeltaTime(),
		PoseJumpThresholdTime, PoseReselectHistory, bShouldSearch ? SearchThrottleTime : UE_BIG_NUMBER, PlayRate, InOutMotionMatchingState,
		InterruptMode, bShouldUseCachedChannelData, bDebugDrawQuery, bDebugDrawCurResult);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UPoseSearchLibrary::UpdateMotionMatchingState(
	const UObject* AnimContext,
	const UE::PoseSearch::IPoseHistory* PoseHistory,
	const TConstArrayView<TObjectPtr<const UPoseSearchDatabase>> Databases,
	float DeltaTime,
	const FFloatInterval& PoseJumpThresholdTime,
	float PoseReselectHistory,
	float SearchThrottleTime,
	const FFloatInterval& PlayRate,
	FMotionMatchingState& InOutMotionMatchingState,
	EPoseSearchInterruptMode InterruptMode,
	bool bShouldUseCachedChannelData,
	bool bDebugDrawQuery,
	bool bDebugDrawCurResult,
	const FPoseSearchEvent& EventToSearch)
{
	FChooserEvaluationContext Context(const_cast<UObject*>(AnimContext));
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	UpdateMotionMatchingState(&Context,
		PoseHistory,
		Databases,
		DeltaTime,
		PoseJumpThresholdTime,
		PoseReselectHistory,
		SearchThrottleTime,
		PlayRate,
		InOutMotionMatchingState,
		InterruptMode,
		bShouldUseCachedChannelData,
		bDebugDrawQuery, bDebugDrawCurResult);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UPoseSearchLibrary::UpdateMotionMatchingState(
	FChooserEvaluationContext* AnimContext,
	const UE::PoseSearch::IPoseHistory* PoseHistory,
	const TConstArrayView<TObjectPtr<const UPoseSearchDatabase>> Databases,
	float DeltaTime,
	const FFloatInterval& PoseJumpThresholdTime,
	float PoseReselectHistory,
	float SearchThrottleTime,
	const FFloatInterval& PlayRate,
	FMotionMatchingState& InOutMotionMatchingState,
	EPoseSearchInterruptMode InterruptMode,
	bool bShouldUseCachedChannelData,
	bool bDebugDrawQuery,
	bool bDebugDrawCurResult,
	const FPoseSearchEvent& EventToSearch)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_PoseSearch_Update);

	using namespace UE::PoseSearch;

	FMemMark Mark(FMemStack::Get());

	const FPoseSearchEvent PlayRateOverriddenEvent = EventToSearch.GetPlayRateOverriddenEvent(PlayRate);
	FSearchContext SearchContext(0.f, PoseJumpThresholdTime, PlayRateOverriddenEvent);
	SearchContext.AddRole(GetCommonDefaultRole(Databases), AnimContext, PoseHistory);

	const UPoseSearchDatabase* CurrentResultDatabase = InOutMotionMatchingState.SearchResult.SelectedDatabase.Get();
	if (IsInvalidatingContinuingPose(InterruptMode, CurrentResultDatabase, Databases))
	{
		InOutMotionMatchingState.SearchResult = FPoseSearchBlueprintResult();
	}
	else
	{
		FSearchResult SearchResult;
		SearchResult.InitFrom(InOutMotionMatchingState.SearchResult);
		SearchContext.UpdateContinuingPoseSearchResult(SearchResult, SearchResult);
	}

	FSearchResult SearchResult;

	const bool bCanAdvance = SearchContext.GetContinuingPoseSearchResult().PoseIdx != INDEX_NONE;

	// If we can't advance or enough time has elapsed since the last pose jump then search
	const bool bSearch = !bCanAdvance || (InOutMotionMatchingState.ElapsedPoseSearchTime >= SearchThrottleTime);
	if (bSearch)
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime = 0.f;
		const bool bForceInterrupt = IsForceInterrupt(InterruptMode, CurrentResultDatabase, Databases);
		const bool bSearchContinuingPose = !bForceInterrupt && bCanAdvance;

		// calculating if it's worth bUseCachedChannelData (if we potentially have to build query with multiple schemas)
		SearchContext.SetUseCachedChannelData(bShouldUseCachedChannelData && ShouldUseCachedChannelData(bSearchContinuingPose ? CurrentResultDatabase : nullptr, Databases));

		FSearchResults_Single SearchResults;

		// Evaluate continuing pose
		if (bSearchContinuingPose)
		{
			CurrentResultDatabase->SearchContinuingPose(SearchContext, SearchResults);
		}

		for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
		{
			if (Database)
			{
				Database->Search(SearchContext, SearchResults);
			}
		}

		SearchResult = SearchResults.GetBestResult();

#if !NO_LOGGING
		if (!SearchResult.IsValid())
		{
			TStringBuilder<1024> StringBuilder;
			StringBuilder << "UPoseSearchLibrary::UpdateMotionMatchingState invalid search result : ForceInterrupt [";
			StringBuilder << bForceInterrupt;
			StringBuilder << "], CanAdvance [";
			StringBuilder << bCanAdvance;
			StringBuilder << "], Indexing [";

			bool bIsIndexing = false;
#if WITH_EDITOR
			bIsIndexing = SearchContext.IsAsyncBuildIndexInProgress();
#endif // WITH_EDITOR
			StringBuilder << bIsIndexing;

			StringBuilder << "], Databases [";

			for (int32 DatabaseIndex = 0; DatabaseIndex < Databases.Num(); ++DatabaseIndex)
			{
				StringBuilder << GetNameSafe(Databases[DatabaseIndex]);
				if (DatabaseIndex != Databases.Num() - 1)
				{
					StringBuilder << ", ";
				}
			}

			StringBuilder << "] ";

			FString String = StringBuilder.ToString();

			if (bIsIndexing)
			{
				UE_LOG(LogPoseSearch, Log, TEXT("%s"), *String);
			}
			else
			{
				UE_LOG(LogPoseSearch, Warning, TEXT("%s"), *String);
			}
		}
#endif // !NO_LOGGING
	}
	else
	{
		InOutMotionMatchingState.ElapsedPoseSearchTime += DeltaTime;
		
		SearchResult = SearchContext.GetContinuingPoseSearchResult();
		SearchResult.bIsContinuingPoseSearch = true;

#if UE_POSE_SEARCH_TRACE_ENABLED
		// in case we skipped the search, we still have to track we would have requested to evaluate Databases and CurrentResultDatabase
		for (const TObjectPtr<const UPoseSearchDatabase>& Database : Databases)
		{
			SearchContext.Track(Database);
		}
		SearchContext.Track(CurrentResultDatabase);
#endif // UE_POSE_SEARCH_TRACE_ENABLED
	}

	const float WantedPlayRate = CalculateWantedPlayRate(SearchResult, SearchContext, PlayRate, PoseHistory ? PoseHistory->GetTrajectorySpeedMultiplier() : 1.f, EventToSearch);
	if (PoseHistory)
	{
		if (const FPoseIndicesHistory* PoseIndicesHistory = PoseHistory->GetPoseIndicesHistory())
		{
			// const casting here is safe since we're in the thread owning the pose history, and it's the correct place to update the previously selected poses
			const_cast<FPoseIndicesHistory*>(PoseIndicesHistory)->Update(SearchResult, DeltaTime, PoseReselectHistory);
		}
	}

	InOutMotionMatchingState.SearchResult.InitFrom(SearchResult, WantedPlayRate);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	InOutMotionMatchingState.WantedPlayRate = WantedPlayRate;
	InOutMotionMatchingState.CurrentSearchResult = SearchResult;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if UE_POSE_SEARCH_TRACE_ENABLED
	TraceMotionMatching(SearchContext, SearchResult, InOutMotionMatchingState.ElapsedPoseSearchTime, DeltaTime, bSearch, InOutMotionMatchingState.SearchResult.WantedPlayRate, InterruptMode);
#endif // UE_POSE_SEARCH_TRACE_ENABLED

#if WITH_EDITORONLY_DATA && ENABLE_ANIM_DEBUG
	if (bDebugDrawQuery || bDebugDrawCurResult)
	{
		const UPoseSearchDatabase* CurResultDatabase = SearchResult.Database.Get();

#if WITH_EDITOR
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(CurResultDatabase, ERequestAsyncBuildFlag::ContinueRequest))
#endif // WITH_EDITOR
		{
			FDebugDrawParams DrawParams(SearchContext.GetContexts(), SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), CurResultDatabase);
			if (bDebugDrawCurResult)
			{
				DrawParams.DrawFeatureVector(SearchResult.PoseIdx);
			}

			if (bDebugDrawQuery)
			{
				DrawParams.DrawFeatureVector(SearchContext.GetOrBuildQuery(CurResultDatabase->Schema));
			}
		}
	}
#endif
}

void UPoseSearchLibrary::IsAnimationAssetLooping(const UObject* Asset, bool& bIsAssetLooping)
{
	if (const UAnimSequenceBase* SequenceBase = Cast<const UAnimSequenceBase>(Asset))
	{
		bIsAssetLooping = SequenceBase->bLoop;
	}
	else if (const UBlendSpace* BlendSpace = Cast<const UBlendSpace>(Asset))
	{
		bIsAssetLooping = BlendSpace->bLoop;
	}
	else if (const UMultiAnimAsset* MultiAnimAsset = Cast<const UMultiAnimAsset>(Asset))
	{
		bIsAssetLooping = MultiAnimAsset->IsLooping();
	}
	else
	{
		bIsAssetLooping = false;
	}
}

void UPoseSearchLibrary::GetDatabaseTags(const UPoseSearchDatabase* Database, TArray<FName>& Tags)
{
	if (Database)
	{
		Tags = Database->Tags;
	}
	else
	{
		Tags.Reset();
	}
}

void UPoseSearchLibrary::MotionMatch(
	UAnimInstance* AnimInstance,
	TArray<UObject*> AssetsToSearch,
	const FName PoseHistoryName,
	const FPoseSearchContinuingProperties ContinuingProperties,
	const FPoseSearchFutureProperties Future,
	FPoseSearchBlueprintResult& Result)
{
	using namespace UE::PoseSearch;

	FMemMark Mark(FMemStack::Get());

	TArray<UAnimInstance*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> AnimInstances;
	AnimInstances.Add(AnimInstance);

	TArray<FName, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> Roles;
	Roles.Add(DefaultRole);

	TArray<const UObject*>& AssetsToSearchConst = reinterpret_cast<TArray<const UObject*>&>(AssetsToSearch);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	MotionMatch(AnimInstances, Roles, AssetsToSearchConst, PoseHistoryName, ContinuingProperties, Future, Result);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<UAnimInstance*> AnimInstances,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UObject*> AssetsToSearch,
	const FName PoseHistoryName,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	FPoseSearchBlueprintResult& Result)
{
	using namespace UE::Anim;
	using namespace UE::PoseSearch;

	Result = FPoseSearchBlueprintResult();

	if (AnimInstances.IsEmpty() || AnimInstances.Num() != Roles.Num())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - invalid input AnimInstances or Roles"));
		return;
	}
	
	for (UAnimInstance* AnimInstance : AnimInstances)
	{
		if (!AnimInstance)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - null AnimInstances"));
			return;
		}

		if (!AnimInstance->CurrentSkeleton)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - null AnimInstances->CurrentSkeleton"));
			return;
		}
	}

	FMemMark Mark(FMemStack::Get());

	TArray<const IPoseHistory*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> PoseHistories;
	TArray<const UObject*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> AnimContexts;
	for (UAnimInstance* AnimInstance : AnimInstances)
	{
		if (const FAnimNode_PoseSearchHistoryCollector_Base* PoseHistoryNode = FindPoseHistoryNode(PoseHistoryName, AnimInstance))
		{
			PoseHistories.Add(&PoseHistoryNode->GetPoseHistory());
		}

		AnimContexts.Add(AnimInstance);
	}

	if (PoseHistories.Num() != AnimInstances.Num())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("UPoseSearchLibrary::MotionMatch - Couldn't find pose history with name '%s'"), *PoseHistoryName.ToString());
		return;
	}

	const FSearchResult SearchResult = MotionMatch(AnimContexts, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, Future, FPoseSearchEvent());
	if (SearchResult.IsValid())
	{
		const UPoseSearchDatabase* Database = SearchResult.Database.Get();
		check(Database);
		
		// figuring out the WantedPlayRate
		float WantedPlayRate = 1.f;
		if (Future.Animation && Future.IntervalTime > 0.f)
		{
			if (const UPoseSearchFeatureChannel_PermutationTime* PermutationTimeChannel = Database->Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_PermutationTime>())
			{
				const FSearchIndex& SearchIndex = Database->GetSearchIndex();
				if (!SearchIndex.IsValuesEmpty())
				{
					TConstArrayView<float> ResultData = Database->GetSearchIndex().GetPoseValues(SearchResult.PoseIdx);
					const float ActualIntervalTime = PermutationTimeChannel->GetPermutationTime(ResultData);
					WantedPlayRate = ActualIntervalTime / Future.IntervalTime;
				}
			}
		}

		Result.InitFrom(SearchResult, WantedPlayRate);
	}
}

UE::PoseSearch::FSearchResult UPoseSearchLibrary::MotionMatch(
	const TArrayView<const UObject*> AnimContexts,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories, 
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	const FPoseSearchEvent& EventToSearch)
{
	using namespace UE::PoseSearch;
	FSearchResults_Single SearchResults;
	MotionMatch(AnimContexts, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, Future, EventToSearch, SearchResults);
	return SearchResults.GetBestResult();
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<const UObject*> AnimContexts,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories, 
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	const FPoseSearchEvent& EventToSearch,
	UE::PoseSearch::FSearchResults& SearchResults)
{
	TArray<FChooserEvaluationContext, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> Contexts;
	const int NumContexts = AnimContexts.Num(); 
	Contexts.SetNum(NumContexts);
	for(int i = 0; i < NumContexts; i++)
	{
		Contexts[i].AddObjectParam(const_cast<UObject*>(AnimContexts[i]));
	}

	MotionMatch(Contexts, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, Future, EventToSearch, SearchResults);
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<const UObject*> AnimContexts,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories, 
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	float DesiredPermutationTimeOffset,
	const FPoseSearchEvent& EventToSearch,
	UE::PoseSearch::FSearchResults& SearchResults)
{
	TArray<FChooserEvaluationContext, TInlineAllocator<UE::PoseSearch::PreallocatedRolesNum>> Contexts;
	const int NumContexts = AnimContexts.Num(); 
	Contexts.SetNum(NumContexts);
	for(int i = 0; i < NumContexts; i++)
	{
		Contexts[i].AddObjectParam(const_cast<UObject*>(AnimContexts[i]));
	}

	MotionMatch(Contexts, Roles, PoseHistories, AssetsToSearch, ContinuingProperties, DesiredPermutationTimeOffset, EventToSearch, SearchResults);
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<FChooserEvaluationContext> Contexts,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories, 
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const FPoseSearchFutureProperties& Future,
	const FPoseSearchEvent& EventToSearch,
	UE::PoseSearch::FSearchResults& SearchResults)
{
	check(!Contexts.IsEmpty() && Contexts.Num() == Roles.Num() && Contexts.Num() == PoseHistories.Num());

	using namespace UE::PoseSearch;

	TArray<const IPoseHistory*, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> InternalPoseHistories;
	InternalPoseHistories = PoseHistories;

	// MemStackPoseHistories will hold future poses to match AssetSamplerBase (at FutureAnimationStartTime) TimeToFutureAnimationStart seconds in the future
	TArray<FMemStackPoseHistory, TInlineAllocator<PreallocatedRolesNum, TMemStackAllocator<>>> MemStackPoseHistories;
	float FutureIntervalTime = Future.IntervalTime;
	if (Future.Animation)
	{
		MemStackPoseHistories.SetNum(InternalPoseHistories.Num());

		float FutureAnimationTime = Future.AnimationTime;
		if (FutureAnimationTime < FiniteDelta)
		{
			UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided Future.AnimationTime (%f) is too small to be able to calculate velocities. Clamping it to minimum value of %f"), FutureAnimationTime, FiniteDelta);
			FutureAnimationTime = FiniteDelta;
		}

		const float MinFutureIntervalTime = FiniteDelta + UE_KINDA_SMALL_NUMBER;
		if (FutureIntervalTime < MinFutureIntervalTime)
		{
			UE_LOG(LogPoseSearch, Warning, TEXT("UPoseSearchLibrary::MotionMatch - provided TimeToFutureAnimationStart (%f) is too small. Clamping it to minimum value of %f"), FutureIntervalTime, MinFutureIntervalTime);
			FutureIntervalTime = MinFutureIntervalTime;
		}

		for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
		{
			if (const IPoseHistory* PoseHistory = InternalPoseHistories[RoleIndex])
			{
				const USkeleton* Skeleton = GetContextSkeleton(Contexts[RoleIndex], false);
				check(Skeleton);
				// @todo: add input BlendParameters to support sampling FutureAnimation blendspaces and support for multi character
				const UAnimationAsset* AnimationAsset = Cast<UAnimationAsset>(Future.Animation);
				if (!AnimationAsset)
				{
					if (const UMultiAnimAsset* MultiAnimAsset = Cast<UMultiAnimAsset>(Future.Animation))
					{
						AnimationAsset = MultiAnimAsset->GetAnimationAsset(Roles[RoleIndex]);
					}
					else
					{
						checkNoEntry();
					}
				}

				MemStackPoseHistories[RoleIndex].Init(InternalPoseHistories[RoleIndex]);
				MemStackPoseHistories[RoleIndex].ExtractAndAddFuturePoses(AnimationAsset, FutureAnimationTime, FiniteDelta, FVector::ZeroVector, FutureIntervalTime, Skeleton);
				InternalPoseHistories[RoleIndex] = MemStackPoseHistories[RoleIndex].GetThisOrPoseHistory();
			}
		}
	}		

	MotionMatch(Contexts, Roles, InternalPoseHistories, AssetsToSearch, ContinuingProperties, FutureIntervalTime, EventToSearch, SearchResults);
}

void UPoseSearchLibrary::MotionMatch(
	const TArrayView<FChooserEvaluationContext> Contexts,
	const TArrayView<const UE::PoseSearch::FRole> Roles,
	const TArrayView<const UE::PoseSearch::IPoseHistory*> PoseHistories,
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	const float DesiredPermutationTimeOffset,
	const FPoseSearchEvent& EventToSearch,
	UE::PoseSearch::FSearchResults& SearchResults)
{
	using namespace UE::PoseSearch;
	
	FSearchContext SearchContext(DesiredPermutationTimeOffset, FFloatInterval(0.f, 0.f), EventToSearch);
	for (int32 RoleIndex = 0; RoleIndex < Roles.Num(); ++RoleIndex)
	{
		SearchContext.AddRole(Roles[RoleIndex], &Contexts[RoleIndex], PoseHistories[RoleIndex]);
	}

	MotionMatch(SearchContext, AssetsToSearch, ContinuingProperties, SearchResults);
}

void UPoseSearchLibrary::MotionMatch(
	UE::PoseSearch::FSearchContext& SearchContext,
	const TArrayView<const UObject*> AssetsToSearch,
	const FPoseSearchContinuingProperties& ContinuingProperties,
	UE::PoseSearch::FSearchResults& SearchResults)
{
	using namespace UE::PoseSearch;

	const FStackAssetSet* CurrentAssetToConsider = nullptr;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (SearchContext.GetInternalDeprecatedAssetsToConsider().IsEmpty())
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		CurrentAssetToConsider = SearchContext.GetAssetsToConsiderSet();
	}
	SearchContext.SetIsContinuingInteraction(ContinuingProperties.bIsContinuingInteraction);

	// collecting all the databases searches in AssetsToSearchPerDatabase
	// and all the continuing pose searches in ContinuingPoseAssetsToSearchPerDatabase
	const bool bUsePoseSearchBranchIn = ShouldUsePoseSearchBranchIn(AssetsToSearch);
	FAssetsToSearchPerDatabase AssetsToSearchPerDatabase;
	FAssetsToSearchPerDatabase ContinuingPoseAssetsToSearchPerDatabase;
	PopulateSearches(AssetsToSearch, SearchContext, AssetsToSearchPerDatabase, bUsePoseSearchBranchIn);
	PopulateContinuingPoseSearches(ContinuingProperties, AssetsToSearch, SearchContext, ContinuingPoseAssetsToSearchPerDatabase, bUsePoseSearchBranchIn);

	FCachedContinuingPoseSearchResults CachedContinuingPoseSearchResults;
	for (const FAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : ContinuingPoseAssetsToSearchPerDatabase)
	{
		const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
		check(Database);

		const bool bInvalidatingContinuingPose = IsInvalidatingContinuingPose(ContinuingProperties.InterruptMode, Database, AssetsToSearchPerDatabase);
		if (!bInvalidatingContinuingPose)
		{
			// reconstructing and caching all the required continuing pose search results
			FSearchResult DatabaseContinuingPoseSearchResult;
			DatabaseContinuingPoseSearchResult.SetAssetTime(ContinuingProperties.PlayingAssetAccumulatedTime);
			DatabaseContinuingPoseSearchResult.PoseIdx = Database->GetPoseIndex(ContinuingProperties.PlayingAsset.Get(), ContinuingProperties.PlayingAssetAccumulatedTime, ContinuingProperties.bIsPlayingAssetMirrored, ContinuingProperties.PlayingAssetBlendParameters);
			DatabaseContinuingPoseSearchResult.Database = Database;
			CachedContinuingPoseSearchResults.CheckedAdd(Database, DatabaseContinuingPoseSearchResult);

			// adding the continuing pose search redult relative to the schema - first instance of the DatabaseContinuingPoseSearchResult 
			// (used to gather the continuing pose search values adopted to create the MM query - relaive to the schema, NOT the database)
			const FSearchResult& SchemaContinuingPoseSearchResult = CachedContinuingPoseSearchResults.FindOrAdd(Database->Schema, DatabaseContinuingPoseSearchResult);

			const bool bForceInterrupt = IsForceInterrupt(ContinuingProperties.InterruptMode, Database, AssetsToSearchPerDatabase);
			const bool bCanAdvance = DatabaseContinuingPoseSearchResult.PoseIdx != INDEX_NONE;
			if (bCanAdvance && !bForceInterrupt)
			{
				SearchContext.UpdateContinuingPoseSearchResult(DatabaseContinuingPoseSearchResult, SchemaContinuingPoseSearchResult);
				Database->SearchContinuingPose(SearchContext, SearchResults);
			}
		}
	}

	// performing all the other databases searches
	for (const FAssetsToSearchPerDatabasePair& AssetsToSearchPerDatabasePair : AssetsToSearchPerDatabase)
	{
		const UPoseSearchDatabase* Database = AssetsToSearchPerDatabasePair.Key;
		check(Database);

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TArray<const UObject*> InternalDeprecatedAssetsToConsider = SearchContext.GetInternalDeprecatedAssetsToConsider();
		TArray<const UObject*> EmptyAssetsToConsider;
		SearchContext.SetInternalDeprecatedAssetsToConsider(EmptyAssetsToConsider);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// in case we haven't searched the continuing pose for this Database, we haven't created and cached the query yet,
		// but if we didn't invalidated the continuing pose (when IsInvalidatingContinuingPose is true), we still can reuse
		// the updated FirstInstanceOfReconstructedContinuingPoseSearchResult data, and by calling UpdateContinuingPoseSearchResult we set the 
		// SearchContext to be able to create a query for Database using the continuing pose data.
		SearchContext.UpdateContinuingPoseSearchResult(CachedContinuingPoseSearchResults.FindOrDefault(Database), CachedContinuingPoseSearchResults.FindOrDefault(Database->Schema));
		SearchContext.SetAssetsToConsiderSet(&AssetsToSearchPerDatabasePair.Value);
		
		Database->Search(SearchContext, SearchResults);
		
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SearchContext.SetInternalDeprecatedAssetsToConsider(InternalDeprecatedAssetsToConsider);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

#if (ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG) || UE_POSE_SEARCH_TRACE_ENABLED
	const FSearchResult SearchResult = SearchResults.GetBestResult();
#endif // (ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG) || UE_POSE_SEARCH_TRACE_ENABLED

#if ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG
	if (SearchResult.IsValid())
	{
		const bool bDrawMatch = GVarAnimMotionMatchDrawMatchEnable;
		const bool bDrawquery = GVarAnimMotionMatchDrawQueryEnable;

		if (bDrawMatch || bDrawquery)
		{
			FDebugDrawParams DrawParams(SearchContext.GetContexts(), SearchContext.GetPoseHistories(), SearchContext.GetRoleToIndex(), SearchResult.Database.Get());
			if (bDrawMatch)
			{
				DrawParams.DrawFeatureVector(SearchResult.PoseIdx);
			}

			if (bDrawquery)
			{
				DrawParams.DrawFeatureVector(SearchContext.GetOrBuildQuery(SearchResult.Database->Schema));
			}
		}
	}
#endif // ENABLE_DRAW_DEBUG && ENABLE_ANIM_DEBUG

#if UE_POSE_SEARCH_TRACE_ENABLED
	TraceMotionMatching(SearchContext, SearchResults, 0.f, 1.f, ContinuingProperties.InterruptMode);
#endif // UE_POSE_SEARCH_TRACE_ENABLED

	// restoring the asset to consider array view
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (SearchContext.GetInternalDeprecatedAssetsToConsider().IsEmpty())
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		SearchContext.SetAssetsToConsiderSet(CurrentAssetToConsider);
	}
}

const FAnimNode_PoseSearchHistoryCollector_Base* UPoseSearchLibrary::FindPoseHistoryNode(
	const FName PoseHistoryName,
	const UAnimInstance* AnimInstance)
{
	if (AnimInstance)
	{
		TSet<const UAnimInstance*, DefaultKeyFuncs<const UAnimInstance*>, TInlineSetAllocator<128>> AlreadyVisited;
		TArray<const UAnimInstance*, TInlineAllocator<128>> ToVisit;

		ToVisit.Add(AnimInstance);
		AlreadyVisited.Add(AnimInstance);

		while (!ToVisit.IsEmpty())
		{
			const UAnimInstance* Visiting = ToVisit.Pop();

			if (IAnimClassInterface* AnimBlueprintClass = IAnimClassInterface::GetFromClass(Visiting->GetClass()))
			{
				if (const FAnimSubsystem_Tag* TagSubsystem = AnimBlueprintClass->FindSubsystem<FAnimSubsystem_Tag>())
				{
					if (const FAnimNode_PoseSearchHistoryCollector_Base* HistoryCollector = TagSubsystem->FindNodeByTag<FAnimNode_PoseSearchHistoryCollector_Base>(PoseHistoryName, Visiting))
					{
						return HistoryCollector;
					}
				}
			}

			const USkeletalMeshComponent* SkeletalMeshComponent = Visiting->GetSkelMeshComponent();
			const TArray<UAnimInstance*>& LinkedAnimInstances = SkeletalMeshComponent->GetLinkedAnimInstances();
			for (const UAnimInstance* LinkedAnimInstance : LinkedAnimInstances)
			{
				bool bIsAlreadyInSet = false;
				AlreadyVisited.Add(LinkedAnimInstance, &bIsAlreadyInSet);
				if (!bIsAlreadyInSet)
				{
					ToVisit.Add(LinkedAnimInstance);
				}
			}
		}
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
