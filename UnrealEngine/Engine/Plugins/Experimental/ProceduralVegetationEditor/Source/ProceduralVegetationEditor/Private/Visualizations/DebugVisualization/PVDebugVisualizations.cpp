// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDebugVisualizations.h"

#include "Facades/PVBranchFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPointFacade.h"

TArray<FVector3f> FPVPointDebugVisualization::GetPivotPositions(const FManagedArrayCollection& InCollection)
{
	const PV::Facades::FPointFacade PointFacade(InCollection);
	const TManagedArray<FVector3f>& PointPositions = PointFacade.GetPositions();

	TArray<FVector3f> Positions;
	for (int32 i = 0; i < PointPositions.Num(); i++)
	{
		const FVector3f& Pos = PointPositions[i];

		Positions.Add(Pos);
	}

	return Positions;
}

void FPVPointDebugVisualization::GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale)
{
	const PV::Facades::FPointFacade PointFacade(InCollection);

	OutPos = PointFacade.GetPosition(InIndex);
	OutScale = PointFacade.GetPointScale(InIndex);
}

TArray<FVector3f> FPVFoliageDebugVisualization::GetPivotPositions(const FManagedArrayCollection& InCollection)
{
	const PV::Facades::FFoliageFacade FoliageFacade(InCollection);
	const TManagedArray<FVector3f>& PointPositions =  FoliageFacade.GetPivotPositions();
	
	TArray<FVector3f> Positions;
	for (int32 i = 0; i < PointPositions.Num(); i++)
	{
		const FVector3f& Pos = PointPositions[i];

		Positions.Add(Pos);
	}

	return Positions;

}

void FPVFoliageDebugVisualization::GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale)
{
	const PV::Facades::FFoliageFacade FoliageFacade(InCollection);
	const PV::Facades::FBranchFacade BranchFacade(InCollection);
	const PV::Facades::FPointFacade PointFacade(InCollection);
	
	PV::Facades::FFoliageEntryData Data =  FoliageFacade.GetFoliageEntry(InIndex);
	OutPos = Data.PivotPoint;

	const TArray<int32>& BranchPoints = BranchFacade.GetPoints(Data.BranchId);

	if (BranchPoints.Num() > 0)
	{
		OutScale = PointFacade.GetPointScale(BranchPoints[0]);
	}
	else
	{
		OutScale = 1;
	}
}

TArray<FVector3f> FPVBranchDebugVisualization::GetPivotPositions(const FManagedArrayCollection& InCollection)
{
	const PV::Facades::FBranchFacade BranchFacade(InCollection);
	const PV::Facades::FPointFacade PointFacade(InCollection);

	TArray<FVector3f> Positions;
	for (int32 BranchIndex = 0; BranchIndex < BranchFacade.GetElementCount(); ++BranchIndex)
	{
		const TArray<int32>& BranchPoints = BranchFacade.GetPoints(BranchIndex);

		if (BranchPoints.Num() > 0)
		{
			FVector3f RootBranchPointPos = PointFacade.GetPosition(BranchPoints[0]);
			Positions.Add(RootBranchPointPos);
		}
	}

	return Positions;
}

void FPVBranchDebugVisualization::GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale)
{
	const PV::Facades::FBranchFacade BranchFacade(InCollection);
	const PV::Facades::FPointFacade PointFacade(InCollection);

	OutPos = FVector3f::ZeroVector;
	OutScale = 1.f;

	const TArray<int32>& BranchPoints = BranchFacade.GetPoints(InIndex);
	if (BranchPoints.Num() > 0)
	{
		const int RootPointIndex = BranchPoints[0];

		if (PointFacade.GetElementCount() > RootPointIndex)
		{
			OutPos = PointFacade.GetPosition(RootPointIndex);
			OutScale = PointFacade.GetPointScale(RootPointIndex);
		}
	}
}