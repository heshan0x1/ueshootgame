// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IShrinkableFacade.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace PV::Facades
{
	/**
	 * FBranchFacade is used to access and manipulate the data from Branch Group with in the ProceduralVegetation's FManagedArrayCollection
	 * A branch is made up of multiple points. Each Branch will know about its parents and children, along with other attributes
	 * Only add the frequently used branch attributes and their access to this facade, for the specific access write a new facade
	 */
	class PROCEDURALVEGETATION_API FBranchFacade final : public IShrinkable
	{
	public:
		FBranchFacade(FManagedArrayCollection& InCollection);
		FBranchFacade(const FManagedArrayCollection& InCollection);

		bool IsConst() const { return Collection == nullptr; }

		bool IsValid() const;

		virtual int32 GetElementCount() const override;

		const TArray<int32>& GetPoints(const int32 Index) const;

		void SetPoints(const int32 Index, const TArray<int32>& InPoints);

		const TArray<int32>& GetChildren(const int32 Index) const;

		void SetChildren(const int32 Index, const TArray<int32>& InChildren);

		const TArray<int32>& GetParents(const int32 Index) const;

		int32 GetParentIndex(const int32 BranchIndex) const;

		int32 GetParentBranchNumber(const int32 BranchIndex) const;

		void SetParents(const int32 Index, const TArray<int32>& InParents);

		int32 GetBranchNumber(const int32 Index) const;

		int32 GetBranchSourceBudNumber(const int32 Index) const;

		TArray<int32> GetParentBranchIndices(int BranchIndex) const;
		
		int32 GetParentBranchIndex(const int32 BranchIndex) const;

		int32 GetHierarchyGenerationNumber(const int32 Index) const;

		const TManagedArray<int32>& GetBranchNumbers() const;

		int32 GetBranchUVMaterial(const int32 Index) const;

		void SetBranchUVMaterial(const int32 Index, const int32 InMaterial);
		
		int32 GetBranchHierarchyNumber(int32 Index) const;

		int32 GetBranchSimulationGroupIndex(int32 Index) const;
		
		void SetBranchSimulationGroupIndex(int32 Index, int32 InLogicalDepth);

		void SetTrunkMaterialPath(const FString& InPath);

		FString GetTrunkMaterialPath() const;

		void SetTrunkURange(const TArray<FVector2f>& InURange);

		const TArray<FVector2f>& GetTrunkURange() const;

		virtual void CopyEntry(const int32 FromIndex, const int32 ToIndex) override;

		virtual void RemoveEntries(const int32 NumEntries, const int32 StartIndex) override;

		void GetSortedBranchIndicesByHierarchy(TArray<int32>& OutSortedIndices) const;
		
		int32 GetBranchIndexFromPointIndex(int32 PointIndex) const;

	private:
		FManagedArrayCollection* Collection = nullptr;

		TManagedArrayAccessor<TArray<int32>> Parents;
		TManagedArrayAccessor<TArray<int32>> Children;
		TManagedArrayAccessor<TArray<int32>> BranchPoints;
		TManagedArrayAccessor<int32> BranchNumbers;
		TManagedArrayAccessor<int32> BranchSourceBudNumber;
		TManagedArrayAccessor<TArray<int32>> BranchFoliageIDs;
		TManagedArrayAccessor<int32> BranchUVMaterial;
		TManagedArrayAccessor<int32> BranchHierarchyNumber;
		TManagedArrayAccessor<int32> BranchSimulationGroupIndex;
		TManagedArrayAccessor<FString> TrunkMaterialPathAttribute;
		TManagedArrayAccessor<TArray<FVector2f>> TrunkURangeAttribute;
		TManagedArrayAccessor<int32> BranchParentNumbers;
		TManagedArrayAccessor<int32> PlantNumbers;
	};
}
