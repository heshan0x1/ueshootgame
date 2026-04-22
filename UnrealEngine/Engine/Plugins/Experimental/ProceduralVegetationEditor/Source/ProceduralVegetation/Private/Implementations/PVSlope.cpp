// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVSlope.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVBudVectorsFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPlantFacade.h"
#include "Facades/PVPointFacade.h"

namespace PVSlope
{
	FQuat4f CalculateBranchRotation(float SlopeDirection, float SlopeAngle)
	{
		const float SlopeDirectionInRadians = FMath::DegreesToRadians(SlopeDirection);
		const float SlopeAngleInRadians = FMath::DegreesToRadians(SlopeAngle);

		const FQuat4f YawRotation = FQuat4f(FVector3f::UpVector, SlopeDirectionInRadians);
		const FQuat4f LeanRotation = FQuat4f(FVector3f::RightVector, SlopeAngleInRadians);
		return YawRotation * LeanRotation;
	}

	TArray<int32> GetBranchSegmentChildren(
		const PV::Facades::FBranchFacade& BranchFacade, 
		const PV::Facades::FPointFacade& PointFacade,
		int32 BranchIndex, 
		int32 BranchPointIndex,
		int32 PreviousBranchPointIndex)
	{
		const TArray<int32>& BranchChildren = BranchFacade.GetChildren(BranchIndex);
		const TManagedArray<int32>& BranchNumbers = BranchFacade.GetBranchNumbers();

		const float BranchPointLengthFromRoot = PointFacade.GetLengthFromRoot(BranchPointIndex);
		const float PreviousBranchPointLengthFromRoot = PointFacade.GetLengthFromRoot(PreviousBranchPointIndex);

		TArray<int32> ChildrenIndices;
		ChildrenIndices.Reserve(BranchChildren.Num());

		for (const int32 BranchChild : BranchChildren)
		{
			const int32 ChildIndex = BranchNumbers.Find(BranchChild);
			if (ChildIndex == INDEX_NONE)
			{
				continue;
			}

			const int32 ParentIndex = BranchFacade.GetParentIndex(ChildIndex);
			if (ParentIndex != BranchIndex)
			{
				continue;
			}

			const TArray<int32>& ChildBranchPoints = BranchFacade.GetPoints(ChildIndex);
			if (ChildBranchPoints.Num() == 0)
			{
				continue;
			}

			const int32 FirstChildPointIndex = ChildBranchPoints[0];
			const float ChildLengthFromRoot = PointFacade.GetLengthFromRoot(FirstChildPointIndex);
			if (ChildLengthFromRoot > BranchPointLengthFromRoot || ChildLengthFromRoot <= PreviousBranchPointLengthFromRoot)
			{
				continue;
			}

			ChildrenIndices.Add(ChildIndex);
		}

		return ChildrenIndices;
	}

	TArray<int32> GetBranchSegmentFoliageEntryIds(
		PV::Facades::FFoliageFacade& FoliageFacade,
		const PV::Facades::FPointFacade& PointFacade,
		int32 BranchIndex,
		int32 BranchPointIndex,
		int32 PreviousBranchPointIndex)
	{
		TArray<int32> OutFoliageEntryIds;

		const float BranchPointLengthFromRoot = PointFacade.GetLengthFromRoot(BranchPointIndex);
		const float PreviousBranchPointLengthFromRoot = PointFacade.GetLengthFromRoot(PreviousBranchPointIndex);

		TArray<int32> FoliageEntryIds = FoliageFacade.GetFoliageEntryIdsForBranch(BranchIndex);
		OutFoliageEntryIds.Reserve(FoliageEntryIds.Num());

		for (const int32 FoliageEntryId : FoliageEntryIds)
		{
			const float FoliageEntryLengthFromRoot = FoliageFacade.GetLengthFromRoot(FoliageEntryId);

			if (FoliageEntryLengthFromRoot <= BranchPointLengthFromRoot && FoliageEntryLengthFromRoot > PreviousBranchPointLengthFromRoot)
			{
				OutFoliageEntryIds.Add(FoliageEntryId);
			}
		}

		return OutFoliageEntryIds;
	}

	void CalcBranchLengthSquared_Recursive(
		const PV::Facades::FBranchFacade& BranchFacade,
		const PV::Facades::FPointFacade& PointFacade,
		int32 BranchIndex,
		const FVector3f& BranchStartPosition,
		float& OutLengthSquared)
	{
		const TArray<int32>& CurrentBranchPoints = BranchFacade.GetPoints(BranchIndex);
		const TManagedArray<FVector3f>& PointPositions = PointFacade.GetPositions();

		for (int32 Index = 0; Index < CurrentBranchPoints.Num(); ++Index)
		{
			const int32 PointIndex = CurrentBranchPoints[Index];
			const int32 PrevPointIndex = Index > 0 ? CurrentBranchPoints[Index - 1] : INDEX_NONE;
			const FVector3f& PointPosition = PointPositions[PointIndex];

			const float PointDistSquared = (PointPosition - BranchStartPosition).SizeSquared();
			if (PointDistSquared > OutLengthSquared)
			{
				OutLengthSquared = PointDistSquared;
			}

			const TArray<int32> BranchSegmentChildren = GetBranchSegmentChildren(BranchFacade, PointFacade, BranchIndex, PointIndex, PrevPointIndex);
			for (const int32 ChildIndex : BranchSegmentChildren)
			{
				CalcBranchLengthSquared_Recursive(BranchFacade, PointFacade, ChildIndex, BranchStartPosition, OutLengthSquared);
			}
		}
	}

	float CalcBranchLength(
		const PV::Facades::FBranchFacade& BranchFacade, 
		const PV::Facades::FPointFacade& PointFacade, 
		int32 BranchIndex)
	{
		const TArray<int32>& CurrentBranchPoints = BranchFacade.GetPoints(BranchIndex);
		if (CurrentBranchPoints.Num() == 0)
		{
			return 0;
		}

		const TManagedArray<FVector3f>& PointPositions = PointFacade.GetPositions();
		const FVector3f& RootPosition = PointPositions[CurrentBranchPoints[0]];

		float BranchLengthSquared = 0;
		FVector3f DebugLargetsPoint = FVector3f::ZeroVector;
		CalcBranchLengthSquared_Recursive(BranchFacade, PointFacade, BranchIndex, RootPosition, BranchLengthSquared);

		return BranchLengthSquared > 0 ? FMath::Sqrt(BranchLengthSquared) : 0.f;
	}

	void ApplySlope_Recursive(
		PV::Facades::FBranchFacade& BranchFacade,
		PV::Facades::FPointFacade& PointFacade,
		PV::Facades::FBudVectorsFacade& BudVectorFacade,
		PV::Facades::FFoliageFacade& FoliageFacade,
		const int32 BranchIndex,
		const FPVSlopeParams& SlopeParams,
		float TreeHeight,
		const FVector3f& TrunkPosition,
		FVector3f ParentPosition,
		FVector3f ParentIdentityPosition
	)
	{
		TManagedArray<FVector3f>& PointPositions = PointFacade.ModifyPositions();
		TManagedArray<TArray<FVector3f>>& PointBudDirections = BudVectorFacade.ModifyBudDirections();

		const TArray<int32>& CurrentBranchPoints = BranchFacade.GetPoints(BranchIndex);

		for (int32 Index = 0; Index < CurrentBranchPoints.Num(); ++Index)
		{
			const int32 PointIndex = CurrentBranchPoints[Index];
			const int32 PrevPointIndex = Index > 0 ? CurrentBranchPoints[Index - 1] : INDEX_NONE;
			FVector3f& Position = PointPositions[PointIndex];

			const float TreeAngleAlpha = 1.0 - (TreeHeight > 0 ? ((Position - TrunkPosition).Size() / TreeHeight) : 0);
			const float AngleStrength = FMath::Clamp(FMath::Pow(TreeAngleAlpha, SlopeParams.BendStrength), 0.0, 1.0);

			const FQuat4f SlopeRotation = CalculateBranchRotation(SlopeParams.SlopeDirection, SlopeParams.SlopeAngle * AngleStrength);

			TArray<FVector3f>& CurrentBudDirections = PointBudDirections[PointIndex];
			for (int32 i = 0; i < CurrentBudDirections.Num(); ++i)
			{
				if (i == PV::Facades::BudDirectionsLightOptimalIndex || i == PV::Facades::BudDirectionsLightSubOptimal)
				{
					continue;
				}

				CurrentBudDirections[i] = SlopeRotation.RotateVector(CurrentBudDirections[i]);
			}

			const TArray<int32> BranchSegmentFoliageEntryIds = GetBranchSegmentFoliageEntryIds(FoliageFacade, PointFacade, BranchIndex, PointIndex, PrevPointIndex);
			for (int32 FoliageEntryId : BranchSegmentFoliageEntryIds)
			{
				const FVector3f FoliageEntryPivotPoint = FoliageFacade.GetPivotPoint(FoliageEntryId);
				const FVector3f NewFoliageEntryPivotPoint = SlopeRotation.RotateVector(FoliageEntryPivotPoint - ParentIdentityPosition) + ParentPosition;
				FoliageFacade.SetPivotPoint(FoliageEntryId, NewFoliageEntryPivotPoint);

				const FVector3f FoliageEntryUpVector = FoliageFacade.GetUpVector(FoliageEntryId);
				const FVector3f NewFoliageEntryUpVector = SlopeRotation.RotateVector(FoliageEntryUpVector);
				FoliageFacade.SetUpVector(FoliageEntryId, NewFoliageEntryUpVector);

				const FVector3f FoliageEntryNormalVector = FoliageFacade.GetNormalVector(FoliageEntryId);
				const FVector3f NewFoliageEntryNormalVector = SlopeRotation.RotateVector(FoliageEntryNormalVector);
				FoliageFacade.SetNormalVector(FoliageEntryId, NewFoliageEntryNormalVector);
			}

			const FVector3f NewPosition = SlopeRotation.RotateVector(Position - ParentIdentityPosition) + ParentPosition;

			ParentIdentityPosition = Position;
			Position = NewPosition;
			ParentPosition = Position;

			const TArray<int32> BranchSegmentChildren = GetBranchSegmentChildren(BranchFacade, PointFacade, BranchIndex, PointIndex, PrevPointIndex);
			for (const int32 ChildIndex : BranchSegmentChildren)
			{
				ApplySlope_Recursive(
					BranchFacade,
					PointFacade,
					BudVectorFacade,
					FoliageFacade,
					ChildIndex,
					SlopeParams,
					TreeHeight,
					TrunkPosition,
					ParentPosition,
					ParentIdentityPosition
				);
			}
		}
	}
}

void FPVSlope::ApplySlope(const FPVSlopeParams& InSlopeParams, FManagedArrayCollection& OutCollection)
{
	PV::Facades::FBranchFacade BranchFacade = PV::Facades::FBranchFacade(OutCollection);
	PV::Facades::FPointFacade PointFacade = PV::Facades::FPointFacade(OutCollection);
	PV::Facades::FBudVectorsFacade BudVectorFacade = PV::Facades::FBudVectorsFacade(OutCollection);
	PV::Facades::FFoliageFacade FoliageFacade = PV::Facades::FFoliageFacade(OutCollection);
	const PV::Facades::FPlantFacade PlantFacade(OutCollection);

	if (!PointFacade.IsValid() || !BranchFacade.IsValid())
	{
		return;
	}

	for (const int32 TrunkIndex : PlantFacade.GetTrunkIndices())
	{
		const TArray<int32>& CurrentBranchPoints = BranchFacade.GetPoints(TrunkIndex);
		if (CurrentBranchPoints.Num() == 0)
		{
			continue;
		}

		const float BranchLength = PVSlope::CalcBranchLength(BranchFacade, PointFacade, TrunkIndex);
		const FVector3f TrunkPosition = PointFacade.GetPosition(CurrentBranchPoints[0]);
		const FVector3f PivotPoint = InSlopeParams.TrunkPivotPoint == EPVSlopeTrunkPivotPoint::Origin ? FVector3f::ZeroVector : TrunkPosition;

		PVSlope::ApplySlope_Recursive(
			BranchFacade,
			PointFacade,
			BudVectorFacade,
			FoliageFacade,
			TrunkIndex,
			InSlopeParams,
			BranchLength,
			TrunkPosition,
			PivotPoint,
			PivotPoint
		);
	}
}
