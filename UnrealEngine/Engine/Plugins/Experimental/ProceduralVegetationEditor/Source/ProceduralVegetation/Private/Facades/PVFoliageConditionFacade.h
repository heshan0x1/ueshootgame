// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayAccessor.h"
#include "Implementations/PVFoliage.h"

struct FPVFoliageInfo;

namespace PV::Facades
{
	struct PROCEDURALVEGETATION_API FFoliageConditonInfo
	{
		FString Name;
		float Weight;
		float Offset;

		EFoliageDistributionCondition GetType() const
		{
			const UEnum* EnumPtr = StaticEnum<EFoliageDistributionCondition>();

			int64 EnumValue = EnumPtr->GetValueByNameString(Name);

			if (EnumValue == INDEX_NONE)
				return EFoliageDistributionCondition::None;

			return static_cast<EFoliageDistributionCondition>(EnumValue);
		}

		FString ToString() const
		{
			return FString::Printf(TEXT("Name = %s, Weight = %f, Reduction = %f"), *Name, Weight, Offset);
		}
	};

	class PROCEDURALVEGETATION_API FFoliageConditionFacade
	{
	public:
		FFoliageConditionFacade(FManagedArrayCollection& InCollection);
		FFoliageConditionFacade(const FManagedArrayCollection& InCollection);

		bool IsValid() const;

		int32 NumEntries() const { return NameAttribute.Num(); };
		
		int32 Add(const FFoliageConditonInfo& InputData);

		void Set(const int32 Index, const FFoliageConditonInfo& InputData);
		void SetData(const TArray<FFoliageConditonInfo>& Infos);
		
		FFoliageConditonInfo GetEntry(const int32 Index) const;

		TArray<FFoliageConditonInfo> GetData() const;

	protected:
		void DefineSchema(FManagedArrayCollection& InCollection);
		
		TManagedArrayAccessor<FString> NameAttribute;
		TManagedArrayAccessor<float> WeightAttribute;
		TManagedArrayAccessor<float> OffsetAttribute;
	};
}
