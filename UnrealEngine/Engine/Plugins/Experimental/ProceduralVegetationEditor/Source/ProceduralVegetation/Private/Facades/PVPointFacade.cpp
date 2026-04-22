// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facades/PVPointFacade.h"
#include "Facades/PVAttributesNames.h"

namespace PV::Facades
{
	FPointFacade::FPointFacade(FManagedArrayCollection& InCollection)
		: Positions(InCollection, AttributeNames::PointPosition, GroupNames::PointGroup)
		, LengthFromRoot(InCollection, AttributeNames::LengthFromRoot, GroupNames::PointGroup)
		, PointScaleGradient(InCollection, AttributeNames::PointScaleGradient, GroupNames::PointGroup)
		, HullGradient(InCollection, AttributeNames::HullGradient, GroupNames::PointGroup)
		, MainTrunkGradient(InCollection, AttributeNames::MainTrunkGradient, GroupNames::PointGroup)
		, GroundGradient(InCollection, AttributeNames::GroundGradient, GroupNames::PointGroup)
		, PointScale(InCollection, AttributeNames::PointScale, GroupNames::PointGroup)
		, BudDirections(InCollection, AttributeNames::BudDirection, GroupNames::PointGroup)
		, BranchGradients(InCollection, AttributeNames::BranchGradient, GroupNames::PointGroup)
		, BudHormoneLevels(InCollection, AttributeNames::BudHormoneLevels, GroupNames::PointGroup)
		, PlantGradients(InCollection, AttributeNames::PlantGradient, GroupNames::PointGroup)
		, LengthFromSeed(InCollection, AttributeNames::LengthFromSeed, GroupNames::PointGroup)
		, BudNumber(InCollection, AttributeNames::BudNumber, GroupNames::PointGroup)
		, NjordPixelIndex(InCollection, AttributeNames::NjordPixelIndex, GroupNames::PointGroup)
		, BudLightDetected(InCollection, AttributeNames::BudLightDetected, GroupNames::PointGroup)
		, BudDevelopment(InCollection, AttributeNames::BudDevelopment, GroupNames::PointGroup)
		, TextureCoordV(InCollection, AttributeNames::TextureCoordV, GroupNames::PointGroup)
		, TextureCoordUOffset(InCollection, AttributeNames::TextureCoordUOffset, GroupNames::PointGroup)
		, URange(InCollection, AttributeNames::URange, GroupNames::PointGroup)
	{
	}

	FPointFacade::FPointFacade(const FManagedArrayCollection& InCollection)
		: Positions(InCollection, AttributeNames::PointPosition, GroupNames::PointGroup)
		, LengthFromRoot(InCollection, AttributeNames::LengthFromRoot, GroupNames::PointGroup)
		, PointScaleGradient(InCollection, AttributeNames::PointScaleGradient, GroupNames::PointGroup)
		, HullGradient(InCollection, AttributeNames::HullGradient, GroupNames::PointGroup)
		, MainTrunkGradient(InCollection, AttributeNames::MainTrunkGradient, GroupNames::PointGroup)
		, GroundGradient(InCollection, AttributeNames::GroundGradient, GroupNames::PointGroup)
		, PointScale(InCollection, AttributeNames::PointScale, GroupNames::PointGroup)
		, BudDirections(InCollection, AttributeNames::BudDirection, GroupNames::PointGroup)
		, BranchGradients(InCollection, AttributeNames::BranchGradient, GroupNames::PointGroup)
		, BudHormoneLevels(InCollection, AttributeNames::BudHormoneLevels, GroupNames::PointGroup)
		, PlantGradients(InCollection, AttributeNames::PlantGradient, GroupNames::PointGroup)
		, LengthFromSeed(InCollection, AttributeNames::LengthFromSeed, GroupNames::PointGroup)
		, BudNumber(InCollection, AttributeNames::BudNumber, GroupNames::PointGroup)
		, NjordPixelIndex(InCollection, AttributeNames::NjordPixelIndex, GroupNames::PointGroup)
		, BudLightDetected(InCollection, AttributeNames::BudLightDetected, GroupNames::PointGroup)
		, BudDevelopment(InCollection, AttributeNames::BudDevelopment, GroupNames::PointGroup)
		, TextureCoordV(InCollection, AttributeNames::TextureCoordV, GroupNames::PointGroup)
		, TextureCoordUOffset(InCollection, AttributeNames::TextureCoordUOffset, GroupNames::PointGroup)
		, URange(InCollection, AttributeNames::URange, GroupNames::PointGroup)
	{
	}

	bool FPointFacade::IsValid() const
	{
		return Positions.IsValid()
			&& LengthFromRoot.IsValid()
			&& LengthFromSeed.IsValid()
			&& PointScaleGradient.IsValid()
			&& HullGradient.IsValid()
			&& MainTrunkGradient.IsValid()
			&& GroundGradient.IsValid()
			&& PointScale.IsValid()
			&& BudDirections.IsValid()
			&& BranchGradients.IsValid()
			&& BudHormoneLevels.IsValid()
			&& PlantGradients.IsValid()
			&& BudNumber.IsValid()
			&& NjordPixelIndex.IsValid()
			&& BudLightDetected.IsValid()
			&& BudDevelopment.IsValid();
	}

	int32 FPointFacade::GetElementCount() const
	{
		return Positions.Num();
	}

	const FVector3f& FPointFacade::GetPosition(const int32 Index) const
	{
		if (Positions.IsValid() && Positions.IsValidIndex(Index))
		{
			return Positions[Index];
		}

		static const FVector3f DefaultPosition = FVector3f::ZeroVector;
		return DefaultPosition;
	}

	const float& FPointFacade::GetLengthFromRoot(const int32 Index) const
	{
		if (LengthFromRoot.IsValid() && LengthFromRoot.IsValidIndex(Index))
		{
			return LengthFromRoot[Index];
		}
		static constexpr float DefaultLengthFromRoot = 0.0;
		return DefaultLengthFromRoot;
	}

	const float& FPointFacade::GetLengthFromSeed(const int32 Index) const
	{
		if (LengthFromSeed.IsValid() && LengthFromSeed.IsValidIndex(Index))
		{
			return LengthFromSeed[Index];
		}
		static constexpr float DefaultLengthFromSeed = 0.0;
		return DefaultLengthFromSeed;
	}

	const float& FPointFacade::GetPointScaleGradient(const int32 Index) const
	{
		if (PointScaleGradient.IsValid() && PointScaleGradient.IsValidIndex(Index))
		{
			return PointScaleGradient[Index];
		}

		static constexpr float DefaultPScale = 0.0;
		return DefaultPScale;
	}

	const float& FPointFacade::GetHullGradient(const int32 Index) const
	{
		if (HullGradient.IsValid() && HullGradient.IsValidIndex(Index))
		{
			return HullGradient[Index];
		}

		static constexpr float DefaultValue = 0.0;
		return DefaultValue;
	}

	const float& FPointFacade::GetMainTrunkGradient(const int32 Index) const
	{
		if (MainTrunkGradient.IsValid() && MainTrunkGradient.IsValidIndex(Index))
		{
			return MainTrunkGradient[Index];
		}

		static constexpr float DefaultValue = 0.0;
		return DefaultValue;
	}

	const float& FPointFacade::GetGroundGradient(const int32 Index) const
	{
		if (GroundGradient.IsValid() && GroundGradient.IsValidIndex(Index))
		{
			return GroundGradient[Index];
		}

		static constexpr float DefaultValue = 0.0;
		return DefaultValue;
	}

	const float& FPointFacade::GetPointScale(const int32 Index) const
	{
		if (PointScale.IsValid() && PointScale.IsValidIndex(Index))
		{
			return PointScale[Index];
		}

		static constexpr float DefaultPScale = 0.0;
		return DefaultPScale;
	}

	const int32 FPointFacade::GetBudNumber(const int32 Index) const
	{
		if (BudNumber.IsValid() && BudNumber.IsValidIndex(Index))
		{
			return BudNumber[Index];
		}

		return INDEX_NONE;
	}
	
	const TArray<float>& FPointFacade::GetBudLightDetected(const int32 Index) const
	{
		if (BudLightDetected.IsValid() && BudLightDetected.IsValidIndex(Index))
		{
			return BudLightDetected[Index];
		}

		static const TArray<float> EmptyArray;
		return EmptyArray;
	}

	const TArray<int>& FPointFacade::GetBudDevelopment(const int32 Index) const
	{
		if (BudDevelopment.IsValid() && BudDevelopment.IsValidIndex(Index))
		{
			return BudDevelopment[Index];
		}

		static const TArray<int> EmptyArray;
		return EmptyArray;
	}

	const TArray<FVector3f>& FPointFacade::GetBudDirection(const int32 Index) const
	{
		if (BudDirections.IsValid() && BudDirections.IsValidIndex(Index))
		{
			return BudDirections[Index];
		}

		static const TArray<FVector3f> EmptyArray;
		return EmptyArray;
	}

	const TArray<float>& FPointFacade::GetBudHormoneLevels(const int32 Index) const
	{
		if (BudHormoneLevels.IsValid() && BudHormoneLevels.IsValidIndex(Index))
		{
			return BudHormoneLevels[Index];
		}

		static const TArray<float> EmptyArray;
		return EmptyArray;
	}

	void FPointFacade::SetBudHormoneLevels(const int32 Index, const TArray<float>& InBudHormoneLevels)
	{
		if (BudHormoneLevels.IsValid() && BudHormoneLevels.IsValidIndex(Index))
		{
			BudHormoneLevels.ModifyAt(Index, InBudHormoneLevels);
		}
	}

	const float& FPointFacade::GetBranchGradient(const int32 Index) const
	{
		if (BranchGradients.IsValid() && BranchGradients.IsValidIndex(Index))
		{
			return BranchGradients[Index];
		}

		static constexpr float DefaultBranchGradient = 0.0;
		return DefaultBranchGradient;
	}

	void FPointFacade::SetBranchGradient(const int32 Index, const float InBranchGradient)
	{
		if (BranchGradients.IsValid() && BranchGradients.IsValidIndex(Index))
		{
			BranchGradients.ModifyAt(Index, InBranchGradient);
		}
	}

	const float& FPointFacade::GetPlantGradient(const int32 Index) const
	{
		if (PlantGradients.IsValid() && PlantGradients.IsValidIndex(Index))
		{
			return PlantGradients[Index];
		}

		static constexpr float DefaultPlantGradient = 0.0;
		return DefaultPlantGradient;
	}

	const float& FPointFacade::GetNjordPixelIndex(const int32 Index) const
	{
		if (NjordPixelIndex.IsValid() && NjordPixelIndex.IsValidIndex(Index))
		{
			return NjordPixelIndex[Index];
		}

		static constexpr float IndexNone = -1.0;
		return IndexNone;
	}

	void FPointFacade::SetPlantGradient(const int32 Index, const float InPlantGradient)
	{
		if (PlantGradients.IsValid() && PlantGradients.IsValidIndex(Index))
		{
			PlantGradients.ModifyAt(Index, InPlantGradient);
		}
	}

	const float& FPointFacade::GetTextureCoordV(const int32 Index) const
	{
		if (TextureCoordV.IsValid() && TextureCoordV.IsValidIndex(Index))
		{
			return TextureCoordV[Index];
		}

		static constexpr float IndexNone = -1.0;
		return IndexNone;
	}

	void FPointFacade::SeTextureCoordV(const int32 Index, const float InTextureCoordV)
	{
		if (!TextureCoordV.IsValid())
		{
			TextureCoordV.Add();
		}
		
		if (TextureCoordV.IsValid() && TextureCoordV.IsValidIndex(Index))
		{
			TextureCoordV.ModifyAt(Index, InTextureCoordV);
		}
	}

	const float& FPointFacade::GetTextureCoordUOffset(const int32 Index) const
	{
		if (TextureCoordUOffset.IsValid() && TextureCoordUOffset.IsValidIndex(Index))
		{
			return TextureCoordUOffset[Index];
		}

		static constexpr float IndexNone = -1.0;
		return IndexNone;
	}

	void FPointFacade::SetTextureCoordUOffset(const int32 Index, const float InTextureCoordUOffset)
	{
		if (!TextureCoordUOffset.IsValid())
		{
			TextureCoordUOffset.Add();
		}
		
		if (TextureCoordUOffset.IsValid() && TextureCoordUOffset.IsValidIndex(Index))
		{
			TextureCoordUOffset.ModifyAt(Index, InTextureCoordUOffset);
		}
	}

	const FVector2f& FPointFacade::GetURange(const int32 Index) const
	{
		if (URange.IsValid() && URange.IsValidIndex(Index))
		{
			return URange[Index];
		}

		static const FVector2f Range(0,1);
		return Range;
	}

	void FPointFacade::SetURange(const int32 Index, const FVector2f InURange)
	{
		if (!URange.IsValid())
		{
			URange.Add();
		}
		
		if (URange.IsValid() && URange.IsValidIndex(Index))
		{
			URange.ModifyAt(Index, InURange);
		}
	}

	TManagedArray<FVector3f>& FPointFacade::ModifyPositions()
	{
		return Positions.Modify();
	}

	TManagedArray<float>& FPointFacade::ModifyPointScales()
	{
		return PointScale.Modify();
	}

	TManagedArray<float>& FPointFacade::ModifyLengthFromRoots()
	{
		return LengthFromRoot.Modify();
	}

	TManagedArray<float>& FPointFacade::ModifyLengthFromSeeds()
	{
		return LengthFromSeed.Modify();
	}

	TManagedArray<float>& FPointFacade::ModifyNjordPixelIDs()
	{
		return NjordPixelIndex.Modify();
	}

	const TManagedArray<FVector3f>& FPointFacade::GetPositions() const
	{
		return Positions.Get();
	}

	const TManagedArray<float>& FPointFacade::GetPointScales() const
	{
		return PointScale.Get();
	}

	const TManagedArray<float>& FPointFacade::GetLengthFromRootsArray() const
	{
		return LengthFromRoot.Get();
	}

	void FPointFacade::CopyEntry(const int32 FromIndex, const int32 ToIndex)
	{
		if (IsValid() && Positions.IsValidIndex(FromIndex) && Positions.IsValidIndex(ToIndex))
		{
			Positions.ModifyAt(ToIndex, Positions[FromIndex]);
			LengthFromRoot.ModifyAt(ToIndex, LengthFromRoot[FromIndex]);
			PointScaleGradient.ModifyAt(ToIndex, PointScaleGradient[FromIndex]);
			HullGradient.ModifyAt(ToIndex, HullGradient[FromIndex]);
			MainTrunkGradient.ModifyAt(ToIndex, MainTrunkGradient[FromIndex]);
			GroundGradient.ModifyAt(ToIndex, GroundGradient[FromIndex]);
			PointScale.ModifyAt(ToIndex, PointScale[FromIndex]);
			BudDirections.ModifyAt(ToIndex, BudDirections[FromIndex]);
			BranchGradients.ModifyAt(ToIndex, BranchGradients[FromIndex]);
			BudHormoneLevels.ModifyAt(ToIndex, BudHormoneLevels[FromIndex]);
			PlantGradients.ModifyAt(ToIndex, PlantGradients[FromIndex]);
			LengthFromSeed.ModifyAt(ToIndex, LengthFromSeed[FromIndex]);
			BudNumber.ModifyAt(ToIndex, BudNumber[FromIndex]);
			NjordPixelIndex.ModifyAt(ToIndex, NjordPixelIndex[FromIndex]);
			BudLightDetected.ModifyAt(ToIndex, BudLightDetected[FromIndex]);
			BudDevelopment.ModifyAt(ToIndex, BudDevelopment[FromIndex]);
			
			if(TextureCoordV.IsValid())
			{
				TextureCoordV.ModifyAt(ToIndex, TextureCoordV[FromIndex]);
			}
			if(TextureCoordUOffset.IsValid())
			{
				TextureCoordUOffset.ModifyAt(ToIndex, TextureCoordUOffset[FromIndex]);
			}
			if(URange.IsValid())
			{
				URange.ModifyAt(ToIndex, URange[FromIndex]);
			}
		}
	}

	void FPointFacade::RemoveEntries(const int32 NumEntries, const int32 StartIndex)
	{
		if (IsValid() && Positions.IsValidIndex(StartIndex) && StartIndex + NumEntries <= Positions.Num())
		{
			Positions.RemoveElements(NumEntries, StartIndex);
		}
	}
}
