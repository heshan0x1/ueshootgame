// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVCarve.h"

#include "Facades/PVAttributesNames.h"
#include "Facades/PVFoliageFacade.h"
#include "Facades/PVPlantFacade.h"
#include "Facades/PVPointFacade.h"
#include "Facades/PVTreeFacade.h"


void FPVCarve::ApplyCarve(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
                          const ECarveBasis CarveBasis, const float Carve)
{
	if (Carve == 0.0f)
	{
		return;
	}

	if (CarveBasis == ECarveBasis::FromBottom)
	{
		CarveFromBottom(OutCollection, SourceCollection, Carve);
	}
	else
	{
		CarveFromTop(OutCollection, SourceCollection, Carve, CarveBasis);
	}
}

void FPVCarve::UpdatePointScales(PV::Facades::FPointFacade& PointFacadeOut, const PV::Facades::FPointFacade& PointFacadeSource,
                                 const TArray<int>& BranchPoints, const float LastPointScale, const int32 LastPointIndex,
                                 const float CarveRatio, const int32 EndIndex, const float FirstPointTargetPScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::UpdatePointScales);

	const float OldMax = FirstPointTargetPScale;
	const float NewMax = OldMax * CarveRatio;
	const float Ratio = NewMax / OldMax;
	const float OldMin = PointFacadeSource.GetPointScale(LastPointIndex);
	const float NewMin = LastPointScale * Ratio;

	for (int32 i = 0; i <= EndIndex; ++i)
	{
		const float UpdatedScale = FMath::GetMappedRangeValueClamped(
			FVector2f(OldMin, OldMax),
			FVector2f(NewMin, NewMax),
			PointFacadeSource.GetPointScale(BranchPoints[i]));
		PointFacadeOut.ModifyPointScales()[BranchPoints[i]] = UpdatedScale;
	}
}

void FPVCarve::RecomputeAttributes(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
                                   const TMap<int32, int32>& BranchesNewIDsToOldIDs)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::RecomputeAttributes);

	const PV::Facades::FBranchFacade BranchFacadeSource(SourceCollection);
	const PV::Facades::FPointFacade PointFacadeSource(SourceCollection);

	const PV::Facades::FBranchFacade BranchFacadeOut(OutCollection);
	const PV::Facades::FPlantFacade PlantFacadeOut(OutCollection);
	PV::Facades::FPointFacade PointFacadeOut(OutCollection);

	for (const int32& PlantNumber : PlantFacadeOut.GetPlantNumbers())
	{
		// Remap Ethylene levels while gathering minimum and maximum PScales for each branch and the whole plant
		float MinimumPScale = TNumericLimits<float>::Max();
		float MaximumPScale = TNumericLimits<float>::Min();
		TArray<TTuple<float, float>> BranchMinMaxPScales;
		BranchMinMaxPScales.Init(TTuple<float, float>(TNumericLimits<float>::Max(), TNumericLimits<float>::Min()), BranchFacadeOut.GetElementCount());

		for (const int32& BranchIndex : PlantFacadeOut.GetBranchIndices(PlantNumber))
		{
			const TArray<int32>& BranchPoints = BranchFacadeOut.GetPoints(BranchIndex);
			const int32 NumBranchPointsCurrent = BranchPoints.Num();
			const int32 OldBranchIndex = BranchesNewIDsToOldIDs.Contains(BranchIndex)
				? BranchesNewIDsToOldIDs[BranchIndex]
				: BranchIndex;
			const TArray<int32>& SourceBranchPoints = BranchFacadeSource.GetPoints(OldBranchIndex);

			for (int32 Idx = 0; Idx < NumBranchPointsCurrent; ++Idx)
			{
				const float RelativePointIndex = (static_cast<float>(Idx) / NumBranchPointsCurrent) * SourceBranchPoints.Num();
				const int32 OldCurrentPointIndex = FMath::Clamp(FMath::CeilToInt(RelativePointIndex), 0, SourceBranchPoints.Num() - 1);
				const int32 OldPreviousPointIndex = OldCurrentPointIndex == 0
					? 0
					: OldCurrentPointIndex - 1;
				float BlendValue = RelativePointIndex - FMath::FloorToInt(RelativePointIndex);

				const TArray<float>& BudHormoneLevelsAtOldPreviousPointIndex = PointFacadeSource.GetBudHormoneLevels(
					SourceBranchPoints[OldPreviousPointIndex]);
				check(BudHormoneLevelsAtOldPreviousPointIndex.Num() >= 5);
				float EthyleneLevelAtOldPreviousPointIndex = BudHormoneLevelsAtOldPreviousPointIndex[4];

				const TArray<float>& BudHormoneLevelsAtOldCurrentPointIndex = PointFacadeSource.GetBudHormoneLevels(
					SourceBranchPoints[OldCurrentPointIndex]);
				check(BudHormoneLevelsAtOldCurrentPointIndex.Num() >= 5);
				float EthyleneLevelAtOldCurrentPointIndex = BudHormoneLevelsAtOldCurrentPointIndex[4];

				const float NewEthyleneLevel = FMath::Lerp(EthyleneLevelAtOldPreviousPointIndex, EthyleneLevelAtOldCurrentPointIndex, BlendValue);
				TArray<float> CurrentBudHormoneLevels = PointFacadeOut.GetBudHormoneLevels(BranchPoints[Idx]);
				check(CurrentBudHormoneLevels.Num() >= 4);
				CurrentBudHormoneLevels[4] = NewEthyleneLevel;
				PointFacadeOut.SetBudHormoneLevels(BranchPoints[Idx], CurrentBudHormoneLevels);

				const float PointScaleAtIndex = PointFacadeOut.GetPointScale(BranchPoints[Idx]);
				MinimumPScale = FMath::Min(MinimumPScale, PointScaleAtIndex);
				MaximumPScale = FMath::Max(MaximumPScale, PointScaleAtIndex);

				BranchMinMaxPScales[BranchIndex].Get<0>() = FMath::Min(BranchMinMaxPScales[BranchIndex].Get<0>(), PointScaleAtIndex);
				BranchMinMaxPScales[BranchIndex].Get<1>() = FMath::Max(BranchMinMaxPScales[BranchIndex].Get<1>(), PointScaleAtIndex);
			}
		}

		// Remap BranchGradient and PlantGradient
		const FVector2f NormalRange(0.0f, 1.0f);
		const FVector2f PlantPScaleRange(MinimumPScale, MaximumPScale);

		for (const int32& BranchIndex : PlantFacadeOut.GetBranchIndices(PlantNumber))
		{
			const TArray<int32>& BranchPoints = BranchFacadeOut.GetPoints(BranchIndex);
			const FVector2f BranchPScaleRange(BranchMinMaxPScales[BranchIndex].Get<0>(), BranchMinMaxPScales[BranchIndex].Get<1>());

			for (const int32& PointIndex : BranchPoints)
			{
				const float PlantGradientAtPointIndex = FMath::GetMappedRangeValueClamped(PlantPScaleRange, NormalRange,
					PointFacadeOut.GetPointScale(PointIndex));
				PointFacadeOut.SetPlantGradient(PointIndex, PlantGradientAtPointIndex);

				const float BranchGradientAtPointIndex = FMath::GetMappedRangeValueClamped(BranchPScaleRange, NormalRange,
					PointFacadeOut.GetPointScale(PointIndex));
				PointFacadeOut.SetBranchGradient(PointIndex, BranchGradientAtPointIndex);
			}
		}
	}
}

void FPVCarve::CarveFromTop(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection, const float Carve,
                            const ECarveBasis CarveBasis)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::CarveFromTop);

	const float CarveDistance = 1.0f - Carve;

	const PV::Facades::FBranchFacade BranchFacadeSource(SourceCollection);
	const PV::Facades::FPointFacade PointFacadeSource(SourceCollection);
	const PV::Facades::FFoliageFacade FoliageFacadeSource(SourceCollection);
	const PV::Facades::FPlantFacade PlantFacadeSource(SourceCollection);

	PV::Facades::FBranchFacade BranchFacadeOut(OutCollection);
	PV::Facades::FPointFacade PointFacadeOut(OutCollection);
	PV::Facades::FFoliageFacade FoliageFacadeOut(OutCollection);

	if (PointFacadeSource.GetElementCount() == 0 || BranchFacadeSource.GetElementCount() == 0)
	{
		return;
	}

	const int32 NumOfBranchesInSource = BranchFacadeSource.GetElementCount();
	TArray<bool> PointsToRemove;
	PointsToRemove.Init(false, PointFacadeSource.GetElementCount());
	TArray<bool> BranchesToRemove;
	BranchesToRemove.Init(false, NumOfBranchesInSource);
	TArray<bool> FoliageInstancesToRemove;
	FoliageInstancesToRemove.Init(false, FoliageFacadeSource.NumFoliageEntries());

	TMap<int32, int32> BranchNumbersToBranchIDs;
	TMap<int32, float> BranchNumbersToLengthFromRoots;
	ComputeMetadata(BranchNumbersToBranchIDs, BranchNumbersToLengthFromRoots, BranchFacadeSource, PointFacadeSource);

	// Compute carve values according to carve basis metric
	TArray<float> CarveValues;
	CarveValues.Reserve(PointFacadeSource.GetElementCount());

	for (int32 i = 0; i < PointFacadeSource.GetElementCount(); ++i)
	{
		switch (CarveBasis)
		{
		case ECarveBasis::LengthFromRoot:
			CarveValues.Add(PointFacadeSource.GetLengthFromRoot(i));
			break;

		case ECarveBasis::FromBottom:
			CarveValues.Add(PointFacadeSource.GetLengthFromRoot(i));
			break;

		case ECarveBasis::ZPosition:
			CarveValues.Add(PointFacadeSource.GetPosition(i).Z);
			break;

		case ECarveBasis::Radius:
			CarveValues.Add(PointFacadeSource.GetPointScale(i));
			break;
		}
	}

	FVector2f MappedRange(0.0f, 1.0f);
	if (CarveBasis == ECarveBasis::Radius)
	{
		MappedRange = FVector2f(1.0f, 0.0f);
	}

	for (const int32& PlantNumber : PlantFacadeSource.GetPlantNumbers())
	{
		float MinimumCarveValue = TNumericLimits<float>::Max();
		float MaximumCarveValue = TNumericLimits<float>::Min();

		for (const int32& BranchIndex : PlantFacadeSource.GetBranchIndices(PlantNumber))
		{
			for (const int32& PointIndex : BranchFacadeSource.GetPoints(BranchIndex))
			{
				MinimumCarveValue = FMath::Min(MinimumCarveValue, CarveValues[PointIndex]);
				MaximumCarveValue = FMath::Max(MaximumCarveValue, CarveValues[PointIndex]);
			}

			for (const int32& PointIndex : BranchFacadeSource.GetPoints(BranchIndex))
			{
				CarveValues[PointIndex] = FMath::GetMappedRangeValueClamped(
					FVector2f(MinimumCarveValue, MaximumCarveValue),
					MappedRange,
					CarveValues[PointIndex]);
			}

			// Compute branches, foliage instances and points to remove
			if (BranchesToRemove[BranchIndex])
			{
				continue;
			}

			const TArray<int32>& BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);
			const int32 NumOfPointsInCurrentBranch = BranchPoints.Num();

			if (NumOfPointsInCurrentBranch == 0)
			{
				BranchesToRemove[BranchIndex] = true;
				continue;
			}

			// Compute PScale for the point in parent branch that is closest to the first point
			// (For non-trunk branches)
			float FirstPointTargetPScale = PointFacadeSource.GetPointScale(BranchPoints[0]);
			if (!PlantFacadeSource.IsTrunkIndex(BranchIndex)) [[likely]]
			{
				const FVector3f& FirstPointPosition = PointFacadeSource.GetPosition(BranchPoints[0]);

				const int32 ParentBranchNumber = BranchFacadeSource.GetParentBranchNumber(BranchIndex);
				const int32 ParentBranchIndex = BranchNumbersToBranchIDs[ParentBranchNumber];
				const TArray<int32>& ParentBranchPoints = BranchFacadeSource.GetPoints(ParentBranchIndex);

				int32 ClosestPointIndex = 0;
				float MinimumDistance = FLT_MAX;
				for (int32 PointIndex = 0; PointIndex < ParentBranchPoints.Num(); ++PointIndex)
				{
					const FVector3f& ParentPointPosition = PointFacadeSource.GetPosition(ParentBranchPoints[PointIndex]);
					if (const float Distance = FVector3f::Distance(FirstPointPosition, ParentPointPosition); Distance < MinimumDistance)
					{
						MinimumDistance = Distance;
						ClosestPointIndex = PointIndex;
					}
				}

				FirstPointTargetPScale = PointFacadeOut.GetPointScale(ParentBranchPoints[ClosestPointIndex]);
			}

			int EndIndex = 1;
			while (EndIndex < NumOfPointsInCurrentBranch && (CarveValues[BranchPoints[EndIndex]] < CarveDistance + 0.0001f))
			{
				EndIndex++;
			}
			EndIndex--;

			if (EndIndex == NumOfPointsInCurrentBranch - 1)
			{
				const int32 LastPointIndex = BranchPoints[EndIndex];
				const float LastPointScale = PointFacadeSource.GetPointScale(LastPointIndex);
				UpdatePointScales(PointFacadeOut, PointFacadeSource, BranchPoints, LastPointScale, LastPointIndex, 1.0f, EndIndex,
					FirstPointTargetPScale);
				continue;
			}

			// Remove all branches, child branches and foliage instances beyond last point
			for (const int32& BranchNumber : BranchFacadeSource.GetChildren(BranchIndex))
			{
				if (BranchNumbersToLengthFromRoots.Contains(BranchNumber) && BranchNumbersToLengthFromRoots[BranchNumber] > PointFacadeSource.
					GetLengthFromRoot(BranchPoints[EndIndex]))
				{
					const int32 Index = BranchNumbersToBranchIDs[BranchNumber];
					BranchesToRemove[Index] = true;
					for (const int32& P : BranchFacadeSource.GetPoints(Index))
					{
						PointsToRemove[P] = true;
					}
					for (const int32& FId : FoliageFacadeSource.GetFoliageEntryIdsForBranch(Index))
					{
						FoliageInstancesToRemove[FId] = true;
					}

					const TArray<int32>& BranchChildren = BranchFacadeSource.GetChildren(Index);
					for (int32 k = 0; k < BranchChildren.Num(); ++k)
					{
						if (!BranchNumbersToBranchIDs.Contains(BranchChildren[k]))
						{
							continue;
						}

						const int32 ChildIndex = BranchNumbersToBranchIDs[BranchChildren[k]];
						BranchesToRemove[ChildIndex] = true;
						for (const int32& P : BranchFacadeSource.GetPoints(ChildIndex))
						{
							PointsToRemove[P] = true;
						}
						for (const int32& FId : FoliageFacadeSource.GetFoliageEntryIdsForBranch(ChildIndex))
						{
							FoliageInstancesToRemove[FId] = true;
						}
					}
				}
			}

			for (const int32& FoliageIndex : FoliageFacadeSource.GetFoliageEntryIdsForBranch(BranchIndex))
			{
				if (FoliageFacadeSource.GetLengthFromRoot(FoliageIndex) > PointFacadeSource.GetLengthFromRoot(BranchPoints[EndIndex]))
				{
					FoliageInstancesToRemove[FoliageIndex] = true;
				}
			}

			// Keep next point to adjust its position, remove all others
			++EndIndex;
			for (int32 j = EndIndex + 1; j < NumOfPointsInCurrentBranch; ++j)
			{
				PointsToRemove[BranchPoints[j]] = true;
			}

			TArray<int32> UpdatedBranchPoints = TArray(&BranchPoints[0], EndIndex + 1);
			BranchFacadeOut.SetPoints(BranchIndex, UpdatedBranchPoints);

			// Adjust last point
			const int32 LastPointIndex = BranchPoints[EndIndex];
			const int32 PreviousPointIndex = BranchPoints[EndIndex - 1];
			const FVector3f& LastPointPosition = PointFacadeSource.GetPosition(LastPointIndex);
			const FVector3f& PreviousPointPosition = PointFacadeSource.GetPosition(PreviousPointIndex);
			const float LastPointCarveValue = CarveValues[LastPointIndex];
			const float PreviousPointCarveValue = CarveValues[PreviousPointIndex];
			const float BlendValue = FMath::Clamp(
				FMath::GetMappedRangeValueClamped(
					FVector2f(PreviousPointCarveValue, LastPointCarveValue),
					FVector2f(0.0f, 1.0f),
					CarveDistance),
				0.1f,
				1.0f);
			const FVector3f Position = FMath::Lerp(PreviousPointPosition, LastPointPosition, BlendValue);
			const float LengthFromRoot = FMath::Lerp(PointFacadeSource.GetLengthFromRoot(PreviousPointIndex),
				PointFacadeSource.GetLengthFromRoot(LastPointIndex),
				BlendValue);
			const float PointScale = FMath::Lerp(PointFacadeSource.GetPointScale(PreviousPointIndex),
				PointFacadeSource.GetPointScale(LastPointIndex),
				BlendValue);
			PointFacadeOut.ModifyPositions()[LastPointIndex] = Position;
			PointFacadeOut.ModifyLengthFromRoots()[LastPointIndex] = LengthFromRoot;
			PointFacadeOut.ModifyLengthFromSeeds()[LastPointIndex] = LengthFromRoot;
			PointFacadeOut.ModifyPointScales()[LastPointIndex] = PointScale;


			// Update point scales
			const float CarveRatio = (EndIndex + BlendValue) / NumOfPointsInCurrentBranch;
			UpdatePointScales(PointFacadeOut, PointFacadeSource, BranchPoints, PointScale, LastPointIndex, CarveRatio, EndIndex,
				FirstPointTargetPScale);
		}
	}

	RemoveEntriesAndRecomputeAttributes(OutCollection, SourceCollection, PointsToRemove, BranchesToRemove, FoliageInstancesToRemove);
}

void FPVCarve::CarveFromBottom(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection, const float Carve)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::CarveFromBottom);

	const PV::Facades::FBranchFacade BranchFacadeSource(SourceCollection);
	const PV::Facades::FPointFacade PointFacadeSource(SourceCollection);
	const PV::Facades::FFoliageFacade FoliageFacadeSource(SourceCollection);
	const PV::Facades::FPlantFacade PlantFacadeSource(SourceCollection);

	PV::Facades::FBranchFacade BranchFacadeOut(OutCollection);
	PV::Facades::FPointFacade PointFacadeOut(OutCollection);
	PV::Facades::FFoliageFacade FoliageFacadeOut(OutCollection);

	if (PointFacadeSource.GetElementCount() == 0 || BranchFacadeSource.GetElementCount() == 0)
	{
		return;
	}

	TArray<bool> PointsToRemove;
	PointsToRemove.Init(false, PointFacadeSource.GetElementCount());
	TArray<bool> BranchesToRemove;
	BranchesToRemove.Init(false, BranchFacadeSource.GetElementCount());
	TArray<bool> FoliageInstancesToRemove;
	FoliageInstancesToRemove.Init(false, FoliageFacadeSource.NumFoliageEntries());

	TMap<int32, int32> BranchNumbersToBranchIDs;
	TMap<int32, float> BranchNumbersToLengthFromRoots;
	ComputeMetadata(BranchNumbersToBranchIDs, BranchNumbersToLengthFromRoots, BranchFacadeSource, PointFacadeSource);

	// Carve along trunk based on length from root
	// Compute branches, foliage instances and points to remove
	for (const TMap<int32, int32> PlantNumbersToTrunkIDs = PlantFacadeSource.GetPlantNumbersToTrunkIndicesMap();
	     const TPair<int32, int32> Pair : PlantNumbersToTrunkIDs)
	{
		const int32 PlantNumber = Pair.Key;
		const int32 TrunkIndex = Pair.Value;
		const TArray<int32>& TrunkPoints = BranchFacadeSource.GetPoints(TrunkIndex);
		const int32 NumOfPointsInTrunk = TrunkPoints.Num();

		if (NumOfPointsInTrunk < 2)
		{
			BranchesToRemove[TrunkIndex] = true;
			continue;
		}

		const float LengthFromRootOfFirstPoint = PointFacadeSource.GetLengthFromRoot(TrunkPoints[0]);
		const float LengthFromRootOfLastPoint = PointFacadeSource.GetLengthFromRoot(TrunkPoints[NumOfPointsInTrunk - 1]);
		const float CarveLengthFromRoot = FMath::GetMappedRangeValueClamped(FVector2f(0.0f, 1.0f),
			FVector2f(LengthFromRootOfFirstPoint, LengthFromRootOfLastPoint), Carve);

		int EndIndex = 0;
		while (EndIndex < NumOfPointsInTrunk && PointFacadeSource.GetLengthFromRoot(TrunkPoints[EndIndex]) < CarveLengthFromRoot)
		{
			EndIndex++;
		}
		EndIndex--;

		// Remove all branches, child branches and foliage instances before current point
		for (const int32& BranchNumber : BranchFacadeSource.GetChildren(TrunkIndex))
		{
			if (BranchNumbersToLengthFromRoots.Contains(BranchNumber) && BranchNumbersToLengthFromRoots[BranchNumber] < CarveLengthFromRoot)
			{
				const int32 Index = BranchNumbersToBranchIDs[BranchNumber];
				BranchesToRemove[Index] = true;
				for (const int32& P : BranchFacadeSource.GetPoints(Index))
				{
					PointsToRemove[P] = true;
				}
				for (const int32& FId : FoliageFacadeSource.GetFoliageEntryIdsForBranch(Index))
				{
					FoliageInstancesToRemove[FId] = true;
				}

				const TArray<int32>& BranchChildren = BranchFacadeSource.GetChildren(Index);
				for (int32 k = 0; k < BranchChildren.Num(); ++k)
				{
					if (!BranchNumbersToBranchIDs.Contains(BranchChildren[k]))
					{
						continue;
					}

					const int32 ChildIndex = BranchNumbersToBranchIDs[BranchChildren[k]];
					BranchesToRemove[ChildIndex] = true;
					for (const int32& P : BranchFacadeSource.GetPoints(ChildIndex))
					{
						PointsToRemove[P] = true;
					}
					for (const int32& FId : FoliageFacadeSource.GetFoliageEntryIdsForBranch(ChildIndex))
					{
						FoliageInstancesToRemove[FId] = true;
					}
				}
			}
		}

		for (const int32& FoliageIndex : FoliageFacadeSource.GetFoliageEntryIdsForBranch(TrunkIndex))
		{
			if (FoliageFacadeSource.GetLengthFromRoot(FoliageIndex) < CarveLengthFromRoot)
			{
				FoliageInstancesToRemove[FoliageIndex] = true;
			}
		}

		// Keep current point to adjust its position, remove all others
		for (int32 j = 0; j < EndIndex; ++j)
		{
			PointsToRemove[TrunkPoints[j]] = true;
		}

		TArray<int32> UpdatedTrunkPoints = TArray(&TrunkPoints[EndIndex], NumOfPointsInTrunk - EndIndex);
		BranchFacadeOut.SetPoints(TrunkIndex, UpdatedTrunkPoints);

		// Adjust End point
		const int32 CurrentPointIndex = TrunkPoints[EndIndex];
		const int32 NextPointIndex = TrunkPoints[EndIndex + 1];
		const FVector3f& PositionOfCurrentPoint = PointFacadeSource.GetPosition(CurrentPointIndex);
		const FVector3f& PositionOfNextPoint = PointFacadeSource.GetPosition(NextPointIndex);
		const float LengthFromRootOfCurrentPoint = PointFacadeSource.GetLengthFromRoot(CurrentPointIndex);
		const float LengthFromRootOfNextPoint = PointFacadeSource.GetLengthFromRoot(NextPointIndex);

		const float BlendValue = FMath::Clamp(
			FMath::GetMappedRangeValueClamped(
				FVector2f(LengthFromRootOfCurrentPoint, LengthFromRootOfNextPoint),
				FVector2f(0.0f, 1.0f),
				CarveLengthFromRoot),
			0.1f,
			1.0f);
		const FVector3f Position = FMath::Lerp(PositionOfCurrentPoint, PositionOfNextPoint, BlendValue);
		const float PointScale = FMath::Lerp(PointFacadeSource.GetPointScale(CurrentPointIndex),
			PointFacadeSource.GetPointScale(NextPointIndex),
			BlendValue);
		PointFacadeOut.ModifyPositions()[CurrentPointIndex] = Position;
		PointFacadeOut.ModifyLengthFromRoots()[CurrentPointIndex] = CarveLengthFromRoot;
		PointFacadeOut.ModifyLengthFromSeeds()[CurrentPointIndex] = CarveLengthFromRoot;
		PointFacadeOut.ModifyPointScales()[CurrentPointIndex] = PointScale;

		// Move all points down and recompute length from roots
		const FVector3f Delta(0.0f, 0.0f, Position.Z);

		for (const int32& BranchIndex : PlantFacadeSource.GetBranchIndices(PlantNumber))
		{
			if (BranchesToRemove[BranchIndex])
			{
				continue;
			}

			for (const int32& PointIndex : BranchFacadeOut.GetPoints(BranchIndex))
			{
				PointFacadeOut.ModifyPositions()[PointIndex] = PointFacadeOut.GetPosition(PointIndex) - Delta;

				PointFacadeOut.ModifyLengthFromRoots()[PointIndex] = FMath::Max(PointFacadeOut.GetLengthFromRoot(PointIndex) - CarveLengthFromRoot,
					0.0f);
				PointFacadeOut.ModifyLengthFromSeeds()[PointIndex] = FMath::Max(PointFacadeOut.GetLengthFromSeed(PointIndex) - CarveLengthFromRoot,
					0.0f);
			}

			for (const int32& FoliageIndex : FoliageFacadeOut.GetFoliageEntryIdsForBranch(BranchIndex))
			{
				const FVector3f UpdatedPivotPoint = FoliageFacadeOut.GetPivotPoint(FoliageIndex) - Delta;
				FoliageFacadeOut.SetPivotPoint(FoliageIndex, UpdatedPivotPoint);

				const float UpdatedLengthFromRoot = FMath::Max(FoliageFacadeOut.GetLengthFromRoot(FoliageIndex) - CarveLengthFromRoot);
				FoliageFacadeOut.SetLengthFromRoot(FoliageIndex, UpdatedLengthFromRoot);
			}
		}
	}

	RemoveEntriesAndRecomputeAttributes(OutCollection, SourceCollection, PointsToRemove, BranchesToRemove, FoliageInstancesToRemove);
}

void FPVCarve::ComputeMetadata(TMap<int32, int32>& OutBranchNumbersToBranchIDs, TMap<int32, float>& OutBranchNumbersToLengthFromRoots,
                               const PV::Facades::FBranchFacade& BranchFacadeSource,
                               const PV::Facades::FPointFacade& PointFacadeSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::ComputeMetadata);

	const int32 NumOfBranchesInSource = BranchFacadeSource.GetElementCount();
	OutBranchNumbersToBranchIDs.Reserve(NumOfBranchesInSource);
	OutBranchNumbersToLengthFromRoots.Reserve(NumOfBranchesInSource);

	for (int32 BranchIndex = 0; BranchIndex < NumOfBranchesInSource; ++BranchIndex)
	{
		const int32 BranchNumber = BranchFacadeSource.GetBranchNumber(BranchIndex);
		OutBranchNumbersToBranchIDs.Add(BranchNumber, BranchIndex);

		const TArray<int32>& BranchPoints = BranchFacadeSource.GetPoints(BranchIndex);
		check(BranchPoints.Num() > 0);

		if (BranchPoints.Num() > 0)
		{
			const int32 FirstPointIndex = BranchFacadeSource.GetPoints(BranchIndex)[0];
			const float BranchLengthFromRoot = PointFacadeSource.GetLengthFromRoot(FirstPointIndex);
			OutBranchNumbersToLengthFromRoots.Add(BranchNumber, BranchLengthFromRoot);
		}
		else
		{
			OutBranchNumbersToLengthFromRoots.Add(BranchNumber, 0.0f);
		}
	}
}

void FPVCarve::RemoveEntriesAndRecomputeAttributes(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
                                                   TArray<bool>& PointsToRemove, TArray<bool>& BranchesToRemove,
                                                   TArray<bool>& FoliageInstancesToRemove)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PVCarve::RemoveEntriesAndRecomputeAttributes);

	PV::Facades::FRemoveEntriesResult RemoveEntriesResult = PV::Facades::FTreeFacade::RemoveEntriesAndReIndexAttributes(
		OutCollection, PointsToRemove, BranchesToRemove, FoliageInstancesToRemove);

	TMap<int32, int32> BranchesNewIDsToOldIDs;
	BranchesNewIDsToOldIDs.Reserve(RemoveEntriesResult.BranchesOldIDsToNewIDs.Num());
	for (const TPair<int32, int32>& Pair : RemoveEntriesResult.BranchesOldIDsToNewIDs)
	{
		BranchesNewIDsToOldIDs.Add(Pair.Value, Pair.Key);
	}

	RecomputeAttributes(OutCollection, SourceCollection, BranchesNewIDsToOldIDs);
}
