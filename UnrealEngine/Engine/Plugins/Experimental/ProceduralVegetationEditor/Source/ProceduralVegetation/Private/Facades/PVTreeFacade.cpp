// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVTreeFacade.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPointFacade.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace PV::Facades
{
	FRemoveEntriesResult FTreeFacade::RemoveEntriesAndReIndexAttributes(FManagedArrayCollection& OutCollection, TArray<bool>& PointsToRemove,
		TArray<bool>& BranchesToRemove, TArray<bool>& FoliageInstancesToRemove)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PV::Facades::FTreeFacade::RemoveEntriesAndReIndexAttributes);

		FBranchFacade BranchFacadeOut(OutCollection);
		FPointFacade PointFacadeOut(OutCollection);
		FFoliageFacade FoliageFacadeOut(OutCollection);

		TMap<int32, int32> PointsOldIDsToNewIDs = IShrinkable::RemoveEntries(PointFacadeOut, PointsToRemove);
		TMap<int32, int32> BranchesOldIDsToNewIDs = IShrinkable::RemoveEntries(BranchFacadeOut, BranchesToRemove);
		TMap<int32, int32> FoliageInstancesOldIDsToNewIDs = IShrinkable::RemoveEntries(FoliageFacadeOut, FoliageInstancesToRemove);

		const int32 NumOfBranches = BranchFacadeOut.GetElementCount();
		TSet<int32> CurrentBranchNumbers;
		CurrentBranchNumbers.Reserve(NumOfBranches);
		for (int32 BranchIndex = 0; BranchIndex < NumOfBranches; ++BranchIndex)
		{
			CurrentBranchNumbers.Add(BranchFacadeOut.GetBranchNumber(BranchIndex));
		}

		for (int32 BranchIndex = 0; BranchIndex < NumOfBranches; ++BranchIndex)
		{
			TArray<int32> UpdatedPoints;
			const TArray<int32>& BranchPoints = BranchFacadeOut.GetPoints(BranchIndex);
			for (const int32& PointIndex : BranchPoints)
			{
				if (PointsOldIDsToNewIDs.Contains(PointIndex))
				{
					UpdatedPoints.Add(PointsOldIDsToNewIDs[PointIndex]);
				}
				else if (!PointsToRemove[PointIndex])
				{
					UpdatedPoints.Add(PointIndex);
				}
			}
			BranchFacadeOut.SetPoints(BranchIndex, UpdatedPoints);

			TArray<int32> UpdatedParents;
			const TArray<int32>& BranchParents = BranchFacadeOut.GetParents(BranchIndex);
			for (const int32& BranchNumber : BranchParents)
			{
				if (BranchNumber == 0 || CurrentBranchNumbers.Contains(BranchNumber))
				{
					UpdatedParents.Add(BranchNumber);
				}
			}
			BranchFacadeOut.SetParents(BranchIndex, UpdatedParents);

			TArray<int32> UpdatedChildren;
			const TArray<int32>& BranchChildren = BranchFacadeOut.GetChildren(BranchIndex);
			for (const int32& BranchNumber : BranchChildren)
			{
				if (CurrentBranchNumbers.Contains(BranchNumber))
				{
					UpdatedChildren.Add(BranchNumber);
				}
			}
			BranchFacadeOut.SetChildren(BranchIndex, UpdatedChildren);

			TArray<int32> UpdatedFoliageInstances;
			const TArray<int32>& FoliageInstances = FoliageFacadeOut.GetFoliageEntryIdsForBranch(BranchIndex);
			for (const int32& FoliageInstanceIndex : FoliageInstances)
			{
				if (FoliageInstancesOldIDsToNewIDs.Contains(FoliageInstanceIndex))
				{
					const int32 NewFoliageInstanceID = FoliageInstancesOldIDsToNewIDs[FoliageInstanceIndex];
					UpdatedFoliageInstances.Add(NewFoliageInstanceID);
					FoliageFacadeOut.SetFoliageBranchId(NewFoliageInstanceID, BranchIndex);
				}
				else if (!FoliageInstancesToRemove[FoliageInstanceIndex])
				{
					UpdatedFoliageInstances.Add(FoliageInstanceIndex);
				}
			}
			FoliageFacadeOut.SetFoliageIdsArray(BranchIndex, UpdatedFoliageInstances);
		}

		FRemoveEntriesResult Result = {
			.PointsOldIDsToNewIDs = MoveTemp(PointsOldIDsToNewIDs),
			.BranchesOldIDsToNewIDs = MoveTemp(BranchesOldIDsToNewIDs),
			.FoliageInstancesOldIDsToNewIDs = MoveTemp(FoliageInstancesOldIDsToNewIDs)
		};

		return Result;
	}

	void FTreeFacade::RemoveBranches(const FBranchFacade& InBranchFacade, TArray<int>& InBranchesToRemove, FManagedArrayCollection& OutCollection)
	{
		FBranchFacade BranchFacadeOut(OutCollection);
		FPointFacade PointFacadeOut(OutCollection);
		FFoliageFacade FoliageFacadeOut(OutCollection);

		for (int32 i = InBranchesToRemove.Num() - 1; i >= 0 ; i--)
		{
			GatherChildBranches(InBranchFacade, InBranchesToRemove[i], InBranchesToRemove);
		}
			
		TArray<bool> BranchesToRemove;
		BranchesToRemove.Init(false, BranchFacadeOut.GetElementCount());

		TArray<bool> PointsToRemove;
		PointsToRemove.Init(false, PointFacadeOut.GetElementCount());
	
		TArray<bool> FoliageInstancesToRemove;
		FoliageInstancesToRemove.Init(false, FoliageFacadeOut.GetElementCount());
	
		for (int32 i = 0; i < InBranchesToRemove.Num(); i++)
		{
			BranchesToRemove[InBranchesToRemove[i]] = true;
		
			for (const int32& P : BranchFacadeOut.GetPoints(InBranchesToRemove[i]))
			{
				PointsToRemove[P] = true;
			}
			for (const int32& FId : FoliageFacadeOut.GetFoliageEntryIdsForBranch(InBranchesToRemove[i]))
			{
				FoliageInstancesToRemove[FId] = true;
			}	
		}

		FRemoveEntriesResult RemoveEntriesResult = RemoveEntriesAndReIndexAttributes(
			OutCollection, PointsToRemove, BranchesToRemove, FoliageInstancesToRemove);
	}

	int32 FTreeFacade::GetGenerationNumberFromSourceBud(const FManagedArrayCollection& Collection, int32 BranchIndex)
	{
		const FBranchFacade BranchFacade(Collection);
		const FPointFacade PointFacade(Collection);
		
		const auto& BranchPoints = BranchFacade.GetPoints(BranchIndex);
		check(BranchPoints.Num() > 0);
		const int32 BranchRootPoint = BranchPoints[0];

		TArray<int32> BudDevelopment = PointFacade.GetBudDevelopment(BranchRootPoint);
		check(BudDevelopment.Num() > 0);
	
		return BudDevelopment[0];
	}

	void FTreeFacade::GatherChildBranches(const FBranchFacade& InBranchFacade, const int InParentIndex, TArray<int32>& OutBranchesToRemove)
	{
		const TArray<int32>& BranchChildren = InBranchFacade.GetChildren(InParentIndex);
		const TManagedArray<int32>& BranchNumbers = InBranchFacade.GetBranchNumbers();
		
		for (const int32& BranchNumber : BranchChildren)
		{
			const int32 BranchIndex = BranchNumbers.Find(BranchNumber);
			
			if (BranchIndex != INDEX_NONE && !OutBranchesToRemove.Contains(BranchIndex))
			{
				OutBranchesToRemove.Add(BranchIndex);
			}

			GatherChildBranches(InBranchFacade, BranchIndex, OutBranchesToRemove);
		}
	}
}
