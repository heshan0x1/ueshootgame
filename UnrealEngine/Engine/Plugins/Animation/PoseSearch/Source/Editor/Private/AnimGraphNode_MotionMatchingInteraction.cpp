// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_MotionMatchingInteraction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_MotionMatchingInteraction)

#define LOCTEXT_NAMESPACE "MotionMatchingInteraction"

UAnimGraphNode_MotionMatchingInteraction::UAnimGraphNode_MotionMatchingInteraction(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

FText UAnimGraphNode_MotionMatchingInteraction::GetTooltipText() const
{
	return LOCTEXT("MotionMatchingInteractionTooltip", "Motion Matching Interaction Node");
}

void UAnimGraphNode_MotionMatchingInteraction::Serialize(FArchive& Ar)
{
	// Handle change of default blend type
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	if (Ar.IsLoading())
	{
		const int32 CustomVersion = Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID);
		if (CustomVersion < FFortniteMainBranchObjectVersion::ChangeDefaultAlphaBlendType)
		{
			// Switch the default back to Linear so old data remains the same
			// Important: this is done before loading so if data has changed from default it still works
			Node.BlendOption = EAlphaBlendOption::Linear;
		}
	}
	
	Super::Serialize(Ar);
}

FText UAnimGraphNode_MotionMatchingInteraction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("MotionMatchingInteraction", "MotionMatchingInteraction");
}

FLinearColor UAnimGraphNode_MotionMatchingInteraction::GetNodeTitleColor() const
{
	return FLinearColor(FColor(6, 9, 53));
}

#undef LOCTEXT_NAMESPACE
