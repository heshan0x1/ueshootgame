// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FManagedArrayCollection;

namespace PV::Facades
{
	struct FRemoveEntriesResult
	{
		TMap<int32, int32> PointsOldIDsToNewIDs;
		TMap<int32, int32> BranchesOldIDsToNewIDs;
		TMap<int32, int32> FoliageInstancesOldIDsToNewIDs;
	};

	class FBranchFacade;

	/**
	 * FTreeFacade is used to remove branches, points, and foliage instances from a tree collection.
	 * It does not validate whether the input data is consistent i.e. the points actually belong to the
	 * same branches that are asking to be removed etc.
	 */
	class PROCEDURALVEGETATION_API FTreeFacade
	{
	public:
		static FRemoveEntriesResult RemoveEntriesAndReIndexAttributes(FManagedArrayCollection& OutCollection, TArray<bool>& PointsToRemove,
		                                                              TArray<bool>& BranchesToRemove, TArray<bool>& FoliageInstancesToRemove);

		static void RemoveBranches(const FBranchFacade& InBranchFacade, TArray<int>& InBranchesToRemove, FManagedArrayCollection& OutCollection);
		
		static int32 GetGenerationNumberFromSourceBud(const FManagedArrayCollection& Collection, int32 BranchIndex);
	private:
		static void GatherChildBranches(const FBranchFacade& InBranchFacade, const int InParentIndex, TArray<int32>& OutBranchesToRemove);
	};
}
