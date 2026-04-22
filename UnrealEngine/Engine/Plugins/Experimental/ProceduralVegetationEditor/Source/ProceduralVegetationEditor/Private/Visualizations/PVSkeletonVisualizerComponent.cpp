// Copyright Epic Games, Inc. All Rights Reserved.


#include "PVSkeletonVisualizerComponent.h"
#include "ProceduralVegetationEditorModule.h"
#include "PrimitiveDrawInterface.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVPointFacade.h"

UPVSkeletonVisualizerComponent::UPVSkeletonVisualizerComponent()
	: SkeletonCollection(nullptr)
{
	bAutoActivate = true;
	bTickInEditor = true;
	PrimaryComponentTick.bCanEverTick = true;
	bUseEditorCompositing = true;
	SetGenerateOverlapEvents(false);
	bIgnoreStreamingManagerUpdate = true;
	
	BBox = FBox(ForceInit);
	Bounds = FBoxSphereBounds(FVector::ZeroVector, FVector::OneVector, 1);
}

const FManagedArrayCollection* UPVSkeletonVisualizerComponent::GetCollection() const
{
	return SkeletonCollection;
}

void UPVSkeletonVisualizerComponent::SetCollection(const FManagedArrayCollection* const InSkeletonCollection)
{
	if (InSkeletonCollection == nullptr)
	{
		return;
	}

	const PV::Facades::FBranchFacade BranchFacade(*InSkeletonCollection);
	const PV::Facades::FPointFacade PointFacade(*InSkeletonCollection);
	if (!BranchFacade.IsValid() || !PointFacade.IsValid())
	{
		UE_LOG(LogProceduralVegetationEditor, Error, TEXT("Failed to setup data viewport, Collection data is not valid for Procedural Vegetation."));
		return;
	}

	SkeletonCollection = InSkeletonCollection;

	const TManagedArray<FVector3f>& PointsPositions = PointFacade.GetPositions();

	InitBounds();
	for (int BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);
		FVector PreviousPosition = FVector(PointsPositions[BranchPoints[0]]);
		BBox += PreviousPosition;

		for (int32 BranchPointIndex = 1; BranchPointIndex < BranchPoints.Num(); ++BranchPointIndex)
		{
			FVector CurrentPosition = FVector(PointsPositions[BranchPoints[BranchPointIndex]]);

			AddLine(PreviousPosition, CurrentPosition, FLinearColor::White, ESceneDepthPriorityGroup::SDPG_MAX, EPointDrawSettings::Start);

			PreviousPosition = CurrentPosition;
		}
	}
}