// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facades/PVBranchFacade.h"
#include "Facades/PVAttributesNames.h"

namespace PV::Facades
{
	FBranchFacade::FBranchFacade(FManagedArrayCollection& InCollection)
		: Collection(&InCollection)
		, Parents(InCollection, AttributeNames::BranchParents, GroupNames::BranchGroup)
		, Children(InCollection, AttributeNames::BranchChildren, GroupNames::BranchGroup)
		, BranchPoints(InCollection, AttributeNames::BranchPoints, GroupNames::BranchGroup)
		, BranchNumbers(InCollection, AttributeNames::BranchNumber, GroupNames::BranchGroup)
		, BranchSourceBudNumber(InCollection, AttributeNames::BranchSourceBudNumber, GroupNames::BranchGroup)
		, BranchFoliageIDs(InCollection, AttributeNames::BranchFoliageIDs, GroupNames::BranchGroup)
		, BranchUVMaterial(InCollection, AttributeNames::BranchUVMaterial, GroupNames::BranchGroup)
		, BranchHierarchyNumber(InCollection, AttributeNames::BranchHierarchyNumber, GroupNames::BranchGroup)
		, BranchSimulationGroupIndex(InCollection, AttributeNames::BranchSimulationGroupIndex, GroupNames::BranchGroup)
		, TrunkMaterialPathAttribute(InCollection, AttributeNames::TrunkMaterialPath, GroupNames::DetailsGroup)
		, TrunkURangeAttribute(InCollection, AttributeNames::TrunkURange, GroupNames::DetailsGroup)
		, BranchParentNumbers(InCollection, AttributeNames::BranchParentNumber, GroupNames::BranchGroup)
		, PlantNumbers(InCollection, AttributeNames::PlantNumber, GroupNames::BranchGroup)
	{
	}

	FBranchFacade::FBranchFacade(const FManagedArrayCollection& InCollection)
		: Collection(nullptr)
		, Parents(InCollection, AttributeNames::BranchParents, GroupNames::BranchGroup)
		, Children(InCollection, AttributeNames::BranchChildren, GroupNames::BranchGroup)
		, BranchPoints(InCollection, AttributeNames::BranchPoints, GroupNames::BranchGroup)
		, BranchNumbers(InCollection, AttributeNames::BranchNumber, GroupNames::BranchGroup)
		, BranchSourceBudNumber(InCollection, AttributeNames::BranchSourceBudNumber, GroupNames::BranchGroup)
		, BranchFoliageIDs(InCollection, AttributeNames::BranchFoliageIDs, GroupNames::BranchGroup)
		, BranchUVMaterial(InCollection, AttributeNames::BranchUVMaterial, GroupNames::BranchGroup)
		, BranchHierarchyNumber(InCollection, AttributeNames::BranchHierarchyNumber, GroupNames::BranchGroup)
		, BranchSimulationGroupIndex(InCollection, AttributeNames::BranchSimulationGroupIndex, GroupNames::BranchGroup)
		, TrunkMaterialPathAttribute(InCollection, AttributeNames::TrunkMaterialPath, GroupNames::DetailsGroup)
		, TrunkURangeAttribute(InCollection, AttributeNames::TrunkURange, GroupNames::DetailsGroup)
		, BranchParentNumbers(InCollection, AttributeNames::BranchParentNumber, GroupNames::BranchGroup)
		, PlantNumbers(InCollection, AttributeNames::PlantNumber, GroupNames::BranchGroup)
	{
	}

	bool FBranchFacade::IsValid() const
	{
		return Parents.IsValid()
			&& Children.IsValid()
			&& BranchPoints.IsValid()
			&& BranchNumbers.IsValid()
			&& BranchSourceBudNumber.IsValid()
			&& BranchHierarchyNumber.IsValid()
			&& BranchParentNumbers.IsValid()
			&& PlantNumbers.IsValid();
	}

	int32 FBranchFacade::GetElementCount() const
	{
		return Parents.Num();
	}

	const TArray<int32>& FBranchFacade::GetPoints(const int32 Index) const
	{
		if (BranchPoints.IsValid() && BranchPoints.IsValidIndex(Index))
		{
			return BranchPoints[Index];
		}

		static const TArray<int32> EmptyPoints;
		return EmptyPoints;
	}

	void FBranchFacade::SetPoints(const int32 Index, const TArray<int32>& InPoints)
	{
		if (BranchPoints.IsValid() && BranchPoints.IsValidIndex(Index))
		{
			BranchPoints.ModifyAt(Index, InPoints);
		}
	}

	const TArray<int32>& FBranchFacade::GetChildren(const int32 Index) const
	{
		if (Children.IsValid() && Children.IsValidIndex(Index))
		{
			return Children[Index];
		}

		static const TArray<int32> EmptyChildren;
		return EmptyChildren;
	}

	void FBranchFacade::SetChildren(const int32 Index, const TArray<int32>& InChildren)
	{
		if (Children.IsValid() && Children.IsValidIndex(Index))
		{
			Children.ModifyAt(Index, InChildren);
		}
	}

	const TArray<int32>& FBranchFacade::GetParents(const int32 Index) const
	{
		if (Parents.IsValid() && Parents.IsValidIndex(Index))
		{
			return Parents[Index];
		}

		static const TArray<int32> EmptyParents;
		return EmptyParents;
	}

	int32 FBranchFacade::GetParentIndex(const int32 BranchIndex) const
	{
		if (BranchParentNumbers.IsValid() && BranchParentNumbers.IsValidIndex(BranchIndex))
		{
			return BranchNumbers.Get().Find(BranchParentNumbers[BranchIndex]);
		}

		return INDEX_NONE;
	}

	int32 FBranchFacade::GetParentBranchNumber(const int32 BranchIndex) const
	{
		if (BranchParentNumbers.IsValid() && BranchParentNumbers.IsValidIndex(BranchIndex))
		{
			return BranchParentNumbers[BranchIndex];
		}

		return INDEX_NONE;
	}

	void FBranchFacade::SetParents(const int32 Index, const TArray<int32>& InParents)
	{
		if (Parents.IsValid() && Parents.IsValidIndex(Index))
		{
			Parents.ModifyAt(Index, InParents);
		}
	}

	int32 FBranchFacade::GetBranchNumber(const int32 Index) const
	{
		if (BranchNumbers.IsValid() && BranchNumbers.IsValidIndex(Index))
		{
			return BranchNumbers[Index];
		}

		return INDEX_NONE;
	}

	int32 FBranchFacade::GetBranchSourceBudNumber(const int32 Index) const
	{
		if (BranchSourceBudNumber.IsValid() && BranchSourceBudNumber.IsValidIndex(Index))
		{
			return BranchSourceBudNumber[Index];
		}

		return INDEX_NONE;
	}

	TArray<int32> FBranchFacade::GetParentBranchIndices(int BranchIndex) const
	{
		TArray<int32> ParentIndices;
		if (IsValid() && Parents.Num() > 0)
		{
			auto ParentsBranchNumbers = Parents[BranchIndex];
			for (auto Parent : ParentsBranchNumbers)
			{
				ParentIndices.Add(BranchNumbers.Get().Find(Parent));
			}
		}

		return ParentIndices;
	}

	int32 FBranchFacade::GetParentBranchIndex(const int32 BranchIndex) const
	{
		if (IsValid() && Parents.IsValidIndex(BranchIndex))
		{
			if (TArray<int32> ParentsBranchNumbers = Parents[BranchIndex];
				ParentsBranchNumbers.Num() > 0)
			{
				const int32 ParentBranchNumber = ParentsBranchNumbers.Last();
				return BranchNumbers.Get().Find(ParentBranchNumber);
			}
		}

		return INDEX_NONE;
	}

	int32 FBranchFacade::GetHierarchyGenerationNumber(const int32 Index) const
	{
		// The generation attributes contain incorrect information at the moment 
		// thus the number of parents can be used to derive the correct number
		if (Parents.IsValid() && Parents.IsValidIndex(Index))
		{
			return Parents[Index].Num();
		}

		return 0;
	}

	const TManagedArray<int32>& FBranchFacade::GetBranchNumbers() const
	{
		return BranchNumbers.Get();
	}

	int32 FBranchFacade::GetBranchUVMaterial(const int32 Index) const
	{
		if (BranchUVMaterial.IsValid() && BranchUVMaterial.IsValidIndex(Index))
		{
			return BranchUVMaterial[Index];
		}

		return INDEX_NONE;
	}

	void FBranchFacade::SetBranchUVMaterial(const int32 Index, const int32 InMaterial)
	{
		if (!BranchUVMaterial.IsValid())
		{
			BranchUVMaterial.Add();
		}
		if (BranchUVMaterial.IsValid() && BranchUVMaterial.IsValidIndex(Index))
		{
			BranchUVMaterial.ModifyAt(Index, InMaterial);
		}
	}

	int32 FBranchFacade::GetBranchHierarchyNumber(const int32 Index) const
	{
		if (BranchHierarchyNumber.IsValid() && BranchHierarchyNumber.IsValidIndex(Index))
		{
			return BranchHierarchyNumber[Index];
		}

		return INDEX_NONE;
	}

	int32 FBranchFacade::GetBranchSimulationGroupIndex(const int32 Index) const
	{
		if (BranchSimulationGroupIndex.IsValid() && BranchSimulationGroupIndex.IsValidIndex(Index))
		{
			return BranchSimulationGroupIndex[Index];
		}

		return INDEX_NONE;
	}

	void FBranchFacade::SetBranchSimulationGroupIndex(const int32 Index, const int32 InSimulationGroupIndex)
	{
		if (!BranchSimulationGroupIndex.IsValid())
		{
			BranchSimulationGroupIndex.Add();
		}
		if (BranchSimulationGroupIndex.IsValid() && BranchSimulationGroupIndex.IsValidIndex(Index))
		{
			BranchSimulationGroupIndex.ModifyAt(Index, InSimulationGroupIndex);
		}
	}

	void FBranchFacade::SetTrunkMaterialPath(const FString& InPath)
	{
		check(!IsConst());

		int32 NumElements = Collection->NumElements(GroupNames::DetailsGroup);

		if (NumElements == 0)
		{
			Collection->AddElements(1, GroupNames::DetailsGroup);
		}
		if (!TrunkMaterialPathAttribute.IsValid())
		{
			TrunkMaterialPathAttribute.Add();
		}

		TrunkMaterialPathAttribute.Modify()[0] = InPath;
	}

	FString FBranchFacade::GetTrunkMaterialPath() const
	{
		if (TrunkMaterialPathAttribute.IsValid() && TrunkMaterialPathAttribute.IsValidIndex(0))
		{
			return TrunkMaterialPathAttribute[0];
		}

		return FString();
	}

	void FBranchFacade::SetTrunkURange(const TArray<FVector2f>& InURange)
	{
		check(!IsConst());

		int32 NumElements = Collection->NumElements(GroupNames::DetailsGroup);

		if (NumElements == 0)
		{
			Collection->AddElements(1, GroupNames::DetailsGroup);
		}
		if (!TrunkURangeAttribute.IsValid())
		{
			TrunkURangeAttribute.Add();
		}

		TrunkURangeAttribute.Modify()[0] = InURange;
	}

	const TArray<FVector2f>& FBranchFacade::GetTrunkURange() const
	{
		if (TrunkURangeAttribute.IsValid() && TrunkURangeAttribute.IsValidIndex(0))
		{
			return TrunkURangeAttribute[0];
		}

		static TArray<FVector2f> TrunkURange;
		return TrunkURange;
	}

	void FBranchFacade::CopyEntry(const int32 FromIndex, const int32 ToIndex)
	{
		if (IsValid() && BranchNumbers.IsValidIndex(FromIndex) && BranchNumbers.IsValidIndex(ToIndex))
		{
			Parents.ModifyAt(ToIndex, Parents[FromIndex]);
			Children.ModifyAt(ToIndex, Children[FromIndex]);
			BranchPoints.ModifyAt(ToIndex, BranchPoints[FromIndex]);
			BranchNumbers.ModifyAt(ToIndex, BranchNumbers[FromIndex]);
			BranchSourceBudNumber.ModifyAt(ToIndex, BranchSourceBudNumber[FromIndex]);
			BranchFoliageIDs.ModifyAt(ToIndex, BranchFoliageIDs[FromIndex]);
			BranchHierarchyNumber.ModifyAt(ToIndex, BranchHierarchyNumber[FromIndex]);
			BranchParentNumbers.ModifyAt(ToIndex, BranchParentNumbers[FromIndex]);
			PlantNumbers.ModifyAt(ToIndex, PlantNumbers[FromIndex]);
			if (BranchUVMaterial.IsValid())
			{
				BranchUVMaterial.ModifyAt(ToIndex, BranchUVMaterial[FromIndex]);
			}
			if (BranchSimulationGroupIndex.IsValid())
			{
				BranchSimulationGroupIndex.ModifyAt(ToIndex, BranchSimulationGroupIndex[FromIndex]);
			}
		}
	}

	void FBranchFacade::RemoveEntries(const int32 NumEntries, const int32 StartIndex)
	{
		if (IsValid() && BranchNumbers.IsValidIndex(StartIndex) && StartIndex + NumEntries <= BranchNumbers.Num())
		{
			BranchNumbers.RemoveElements(NumEntries, StartIndex);
		}
	}

	void FBranchFacade::GetSortedBranchIndicesByHierarchy(TArray<int32>& OutSortedIndices) const
	{
		for (int32 Index = 0; Index < GetElementCount(); Index++)
		{
			OutSortedIndices.Add(Index);
		}

		OutSortedIndices.Sort([&](int32 Index1, int32 Index2)
			{
				int32 Index1Hierarchy = GetBranchHierarchyNumber(Index1);
				int32 Index2Hierarchy = GetBranchHierarchyNumber(Index2);

				if (Index1Hierarchy == Index2Hierarchy)
				{
					return Index1 < Index2;
				}

				return Index1Hierarchy < Index2Hierarchy;
			});
	}

	int32 FBranchFacade::GetBranchIndexFromPointIndex(const int32 PointIndex) const
	{
		for (int BranchIndex = 0; BranchIndex < GetElementCount(); BranchIndex++)
		{
			TArray<int32> PointInexes = GetPoints(BranchIndex);
			if (PointInexes.Contains(PointIndex))
			{
				return BranchIndex;
			}
		}

		return INDEX_NONE;
	}
}
