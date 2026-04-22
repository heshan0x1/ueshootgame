// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IShrinkableFacade.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace PV::Facades
{
	/**
	 * FPointFacade is used to access and manipulate the Point Group data from the ProceduralVegetation's FManagedArrayCollection
	 * Only add the frequently used Point attributes and their access to this facade, for the specific access write a new facade
	 */
	class PROCEDURALVEGETATION_API FPointFacade final : public IShrinkable
	{
	public:
		FPointFacade(FManagedArrayCollection& InCollection);
		FPointFacade(const FManagedArrayCollection& InCollection);

		bool IsValid() const;

		virtual int32 GetElementCount() const override;

		const FVector3f& GetPosition(const int32 Index) const;

		const float& GetLengthFromRoot(const int32 Index) const;

		const float& GetLengthFromSeed(const int32 Index) const;

		const float& GetPointScaleGradient(const int32 Index) const;

		const float& GetHullGradient(const int32 Index) const;

		const float& GetMainTrunkGradient(const int32 Index) const;

		const float& GetGroundGradient(const int32 Index) const;
		
		const float& GetPointScale(const int32 Index) const;

		const int32 GetBudNumber(const int32 Index) const;

		const TArray<float>& GetBudLightDetected(const int32 Index) const;

		const TArray<int>& GetBudDevelopment(const int32 Index) const;
		
		const TArray<FVector3f>& GetBudDirection(const int32 Index) const;

		const TArray<float>& GetBudHormoneLevels(const int32 Index) const;

		void SetBudHormoneLevels(const int32 Index, const TArray<float>& InBudHormoneLevels);

		const float& GetBranchGradient(const int32 Index) const;

		void SetBranchGradient(const int32 Index, const float InBranchGradient);

		const float& GetPlantGradient(const int32 Index) const;

		const float& GetNjordPixelIndex(const int32 Index) const;

		void SetPlantGradient(const int32 Index, const float InPlantGradient);

		const float& GetTextureCoordV(int32 Index) const;
		
		void SeTextureCoordV(int32 Index, float InTextureCoordV);
		
		const float& GetTextureCoordUOffset(int32 Index) const;
		
		void SetTextureCoordUOffset(int32 Index, float InTextureCoordUOffset);
		
		const FVector2f& GetURange(int32 Index) const;
		
		void SetURange(int32 Index, FVector2f InURange);

		TManagedArray<FVector3f>& ModifyPositions();
		
		TManagedArray<float>& ModifyPointScales();

		TManagedArray<float>& ModifyLengthFromRoots();
		
		TManagedArray<float>& ModifyLengthFromSeeds();
		
		TManagedArray<float>& ModifyNjordPixelIDs();

		const TManagedArray<FVector3f>& GetPositions() const;
		
		const TManagedArray<float>& GetPointScales() const;

		const TManagedArray<float>& GetLengthFromRootsArray() const;

		virtual void CopyEntry(const int32 FromIndex, const int32 ToIndex) override;

		virtual void RemoveEntries(const int32 NumEntries, const int32 StartIndex) override;

	private:
		TManagedArrayAccessor<FVector3f> Positions;
		TManagedArrayAccessor<float> LengthFromRoot;
		TManagedArrayAccessor<float> PointScaleGradient;
		TManagedArrayAccessor<float> HullGradient;
		TManagedArrayAccessor<float> MainTrunkGradient;
		TManagedArrayAccessor<float> GroundGradient;
		TManagedArrayAccessor<float> PointScale;
		TManagedArrayAccessor<TArray<FVector3f>> BudDirections;
		TManagedArrayAccessor<float> BranchGradients;
		TManagedArrayAccessor<TArray<float>> BudHormoneLevels;
		TManagedArrayAccessor<float> PlantGradients;
		TManagedArrayAccessor<float> LengthFromSeed;
		TManagedArrayAccessor<int32> BudNumber;
		TManagedArrayAccessor<float> NjordPixelIndex;
		TManagedArrayAccessor<TArray<float>> BudLightDetected;
		TManagedArrayAccessor<TArray<int>> BudDevelopment;
		TManagedArrayAccessor<float> TextureCoordV;
		TManagedArrayAccessor<float> TextureCoordUOffset;
		TManagedArrayAccessor<FVector2f> URange;
	};
}
