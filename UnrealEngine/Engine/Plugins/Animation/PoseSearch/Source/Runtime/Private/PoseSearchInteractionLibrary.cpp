// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchInteractionSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchInteractionLibrary)

FPoseSearchBlueprintResult UPoseSearchInteractionLibrary::MotionMatchInteraction_Pure(TArray<FPoseSearchInteractionAvailability> Availabilities, const UObject* AnimContext, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities)
{
	FPoseSearchBlueprintResult Result;
	MotionMatchInteraction(Result, Availabilities, AnimContext, PoseHistoryName, nullptr, bValidateResultAgainstAvailabilities);
	return Result;
}

FPoseSearchBlueprintResult UPoseSearchInteractionLibrary::MotionMatchInteraction(TArray<FPoseSearchInteractionAvailability> Availabilities, const UObject* AnimContext, FName PoseHistoryName, bool bValidateResultAgainstAvailabilities)
{
	FPoseSearchBlueprintResult Result;
	MotionMatchInteraction(Result, Availabilities, AnimContext, PoseHistoryName, nullptr, bValidateResultAgainstAvailabilities);
	return Result;
}

void UPoseSearchInteractionLibrary::MotionMatchInteraction(FPoseSearchBlueprintResult& Result, const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, const UObject* AnimContext, FName PoseHistoryName, const UE::PoseSearch::IPoseHistory* PoseHistory, bool bValidateResultAgainstAvailabilities)
{
	if (UPoseSearchInteractionSubsystem* InteractionSubsystem = UPoseSearchInteractionSubsystem::GetSubsystem_AnyThread(AnimContext))
	{
		InteractionSubsystem->Query_AnyThread(Availabilities, AnimContext, Result, PoseHistoryName, PoseHistory, bValidateResultAgainstAvailabilities);
	}
	else
	{
		Result = FPoseSearchBlueprintResult();
	}
}

bool UPoseSearchInteractionLibrary::MotionMatchInteraction(FPoseSearchBlueprintResult& InOutResult, const TArrayView<const FPoseSearchInteractionAvailability> Availabilities, const UObject* AnimContext, FName PoseHistoryName, const UE::PoseSearch::IPoseHistory* PoseHistory, bool bValidateResultAgainstAvailabilities, bool bKeepInteractionAlive, float BlendTime)
{
	using namespace UE::PoseSearch;
	
	bool bIsInteraction = false;
	if (!Availabilities.IsEmpty())
	{
		CheckInteractionThreadSafety(AnimContext);

		FPoseSearchBlueprintResult SearchResult;
		UPoseSearchInteractionLibrary::MotionMatchInteraction(SearchResult, Availabilities, AnimContext, PoseHistoryName, PoseHistory, bValidateResultAgainstAvailabilities);
		check(SearchResult.ActorRootTransforms.Num() == SearchResult.ActorRootBoneTransforms.Num());

		if (SearchResult.SelectedAnim)
		{
			bIsInteraction = true;
			InOutResult = SearchResult;
		}
	}

	// performing the regular single character motion matching search in case there's no MM interaction
	if (!bIsInteraction)
	{
		if (!InOutResult.bIsInteraction)
		{
			// do nothing
		}
		else if (bKeepInteractionAlive)
		{
			// checking if the kept alive interaction has reached the end of animation
			if (!InOutResult.SelectedDatabase || INDEX_NONE == InOutResult.SelectedDatabase->GetPoseIndex(InOutResult.SelectedAnim.Get(), InOutResult.SelectedTime, InOutResult.bIsMirrored, InOutResult.BlendParameters))
			{
				InOutResult = FPoseSearchBlueprintResult();
			}
			else
			{
				// letting the interaction animation run until its length minus blend time
				// (to avoid having to blend from a frozen animation that reached its end for the entire duration of the blend)
				const UAnimationAsset* AnimationAssetForRole = InOutResult.GetAnimationAssetForRole();
				if (!AnimationAssetForRole || InOutResult.SelectedTime >= (AnimationAssetForRole->GetPlayLength() - BlendTime))
				{
					InOutResult = FPoseSearchBlueprintResult();
				}
				else
				{
					// we're keeping alive only the animation part of the search result
					InOutResult.ActorRootTransforms.Reset();
					InOutResult.ActorRootBoneTransforms.Reset();
					InOutResult.AnimContexts.Reset();
				}
			}
		}
		else
		{
			InOutResult = FPoseSearchBlueprintResult();
		}
	}

	return bIsInteraction;
}

FPoseSearchBlueprintResult UPoseSearchInteractionLibrary::GetCachedInteraction(const UObject* AnimContext, bool bCompareOwningActors)
{
	FPoseSearchBlueprintResult Result;
	if (UPoseSearchInteractionSubsystem* InteractionSubsystem = UPoseSearchInteractionSubsystem::GetSubsystem_AnyThread(AnimContext))
	{
		InteractionSubsystem->GetResult_AnyThread(AnimContext, Result, bCompareOwningActors);
	}
	return Result;
}

FPoseSearchContinuingProperties UPoseSearchInteractionLibrary::GetMontageContinuingProperties(UAnimInstance* AnimInstance)
{
	FPoseSearchContinuingProperties ContinuingProperties;
	if (const FAnimMontageInstance* AnimMontageInstance = AnimInstance->GetActiveMontageInstance())
	{
		ContinuingProperties.PlayingAsset = AnimMontageInstance->Montage;
		ContinuingProperties.PlayingAssetAccumulatedTime = AnimMontageInstance->DeltaTimeRecord.GetPrevious();
	}
	return ContinuingProperties;
}
