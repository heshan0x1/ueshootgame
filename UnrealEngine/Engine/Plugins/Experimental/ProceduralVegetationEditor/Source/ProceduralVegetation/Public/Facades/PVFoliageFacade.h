// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IShrinkableFacade.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

struct FPVFoliageInfo;

namespace PV::Facades
{
	struct PROCEDURALVEGETATION_API FFoliageEntryData
	{
		int32 NameId;
		int32 BranchId;
		FVector3f PivotPoint;
		FVector3f UpVector;
		FVector3f NormalVector;
		float Scale;
		float LengthFromRoot;
		float ConditionUpAlignment;
		float ConditionTip;
		float ConditionLight;
		float ConditionScale;
		float ConditionHealth;
		int32 ParentBoneID = INDEX_NONE;
	};

	/**
	 * FFoliageFacade is used to access and manipulate Foliage related data for a ProceduralVegetation's FManagedArrayCollection
	 * It presents access to foliage Mesh names, their respective Branch ids, and instancing transform data (Pivot point, Up Vector, and Scale)
	 * It lays out data in two Groups: FoliageNames and Foliage where Foliage references FoliageNames and Branch/Primitives with Ids
	 * It also adds Foliage entry Ids to the Branch/Primitives group. 
	 */
	class PROCEDURALVEGETATION_API FFoliageFacade final : public IShrinkable
	{
	public:
		FFoliageFacade(FManagedArrayCollection& InCollection, const int32 InitialSize = 0);
		FFoliageFacade(const FManagedArrayCollection& InCollection);

		bool IsConst() const { return Collection == nullptr; }

		bool IsValid() const;

		int32 NumFoliageEntries() const { return NameIdsAttribute.Num(); };

		FFoliageEntryData GetFoliageEntry(const int32 Index) const;

		const float& GetLengthFromRoot(const int32 Index) const;

		void SetLengthFromRoot(const int32 Index, const float Input);

		int32 GetParentBoneID(int32 Index) const;

		void SetParentBoneID(int32 Index, int32 Input);

		const TArray<int32>& GetFoliageEntryIdsForBranch(int32 Index) const;

		int32 AddFoliageEntry(const FFoliageEntryData& InputData);

		void SetFoliageEntry(const int32 Index, const FFoliageEntryData& InputData);

		void SetFoliageIdsArray(const int32 Index, const TArray<int32>& InputIds);

		void SetFoliageBranchId(const int32 Index, const int32 InputId);

		int32 NumFoliageNames() const { return FoliageInfoNamesAttribute.Num(); };

		FString GetFoliageName(const int32 Index) const;

		TArray<FString> GetFoliageNames() const;

		int32 NumFoliageInfo() const { return FoliageInfoNamesAttribute.Num(); };

		FPVFoliageInfo GetFoliageInfo(const int32 Index) const;

		TArray<FPVFoliageInfo> GetFoliageInfos() const;

		const FVector3f& GetPivotPoint(const int32 Index) const;

		const FVector3f& GetUpVector(const int32 Index) const;

		const FVector3f& GetNormalVector(const int32 Index) const;

		void SetPivotPoint(const int32 Index, const FVector3f& Input);

		void SetUpVector(const int32 Index, const FVector3f& Input);

		void SetNormalVector(const int32 Index, const FVector3f& Input);

		void SetScale(const int32 Index, const float& Input);

		void SetFoliageName(const int32 Index, const FString& Input);

		void SetFoliageNames(const TArray<FString>& InputNames);

		void SetFoliageInfo(const int32 Index, const FString& Input);

		void SetFoliageInfos(const TArray<FPVFoliageInfo>& InputNames);

		void SetFoliagePath(const FString InPath);

		FString GetFoliagePath() const;

		virtual int32 GetElementCount() const override;

		virtual void CopyEntry(const int32 FromIndex, const int32 ToIndex) override;

		virtual void RemoveEntries(const int32 NumEntries, const int32 StartIndex) override;
		
		FTransform GetFoliageTransform(int32 Id) const;

		const TManagedArray<FVector3f>& GetPivotPositions() const;

	protected:
		void DefineSchema(const int32 InitialSize = 0);

		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;

		TManagedArrayAccessor<FString> FoliageInfoNamesAttribute;
		TManagedArrayAccessor<float> FoliageInfoLightAttribute;
		TManagedArrayAccessor<float> FoliageInfoScaleAttribute;
		TManagedArrayAccessor<float> FoliageInfoTipAttribute;
		TManagedArrayAccessor<float> FoliageInfoUpAlignmentAttribute;
		TManagedArrayAccessor<float> FoliageInfoHealthAttribute;
		
		TManagedArrayAccessor<int32> NameIdsAttribute;
		TManagedArrayAccessor<int32> BranchIdsAttribute;
		TManagedArrayAccessor<FVector3f> PivotPointsAttribute;
		TManagedArrayAccessor<FVector3f> UpVectorsAttribute;
		TManagedArrayAccessor<FVector3f> NormalVectorsAttribute;
		TManagedArrayAccessor<float> ScalesAttribute;
		TManagedArrayAccessor<float> LengthFromRootAttribute;
		TManagedArrayAccessor<float> ConditionLightAttribute;
		TManagedArrayAccessor<float> ConditionScaleAttribute;
		TManagedArrayAccessor<float> ConditionTipAttribute;
		TManagedArrayAccessor<float> ConditionUpAlignmentAttribute;
		TManagedArrayAccessor<float> ConditionHealthAttribute;
		TManagedArrayAccessor<int32> ParentBoneIdsAttribute;
		TManagedArrayAccessor<TArray<int32>> FoliageIdsAttribute;
		TManagedArrayAccessor<FString> FoliagePathAttribute;
	};
}
