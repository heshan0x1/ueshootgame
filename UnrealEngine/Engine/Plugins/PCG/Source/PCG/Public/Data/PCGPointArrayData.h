// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGBasePointData.h"
#include "PCGPointArray.h"

#include "PCGPointArrayData.generated.h"

#define UE_API PCG_API

extern PCG_API TAutoConsoleVariable<bool> CVarPCGEnablePointArrayDataParenting;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class UPCGPointArrayData : public UPCGBasePointData
{
	GENERATED_BODY()
public:	
	//~Begin UObject Interface
	UE_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	//~End UObject Interface

	UE_API virtual void Flatten() override;

	UE_API virtual void VisitDataNetwork(TFunctionRef<void(const UPCGData*)> Action) const override;
	UE_API virtual const UPCGPointData* ToPointData(FPCGContext* Context, const FBox& InBounds = FBox(EForceInit::ForceInit)) const override;
	virtual const UPCGPointArrayData* ToPointArrayData(FPCGContext* Context, const FBox& InBounds = FBox(EForceInit::ForceInit)) const override { return this; }
	UE_API virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
	UE_API virtual void CopyPropertiesTo(UPCGBasePointData* To, int32 ReadStartIndex, int32 WriteStartIndex, int32 Count, EPCGPointNativeProperties Properties) const override;
	UE_API virtual EPCGPointNativeProperties GetAllocatedProperties(bool bWithInheritance = true) const override;
	UE_API virtual bool SupportsSpatialDataInheritance() const override;
	virtual bool HasSpatialDataParent() const override { return ParentData != nullptr; }

	//~Begin UPCGBasePointData Interface
	virtual bool IsValidRef(const PCGPointOctree::FPointRef& InPointRef) const override { return InPointRef.Index >= 0 && InPointRef.Index < GetNumPoints(); }
	
	virtual int32 GetNumPoints() const override { return PointArray.GetNumPoints(); }
	UE_API virtual void SetNumPoints(int32 InNumPoints, bool bInitializeValues = true) override;
	UE_API void RemoveAt(int32 Index);
	
	UE_API virtual void AllocateProperties(EPCGPointNativeProperties Properties) override;
	UE_API virtual void FreeProperties(EPCGPointNativeProperties Properties) override;
	UE_API virtual void MoveRange(int32 RangeStartIndex, int32 MoveToIndex, int32 NumElements) override;
	UE_API virtual void CopyUnallocatedPropertiesFrom(const UPCGBasePointData* InPointData) override;

	virtual TArray<FTransform> GetTransformsCopy() const { return PointArray.GetTransformCopy(); }

	UE_API virtual FPCGPointTransform::ValueRange GetTransformValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointDensity::ValueRange GetDensityValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointBoundsMin::ValueRange GetBoundsMinValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointBoundsMax::ValueRange GetBoundsMaxValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointColor::ValueRange GetColorValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointSteepness::ValueRange GetSteepnessValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointSeed::ValueRange GetSeedValueRange(bool bAllocate = true) override;
	UE_API virtual FPCGPointMetadataEntry::ValueRange GetMetadataEntryValueRange(bool bAllocate = true) override;

	UE_API virtual FPCGPointTransform::ConstValueRange GetConstTransformValueRange() const override;
	UE_API virtual FPCGPointDensity::ConstValueRange GetConstDensityValueRange() const override;
	UE_API virtual FPCGPointBoundsMin::ConstValueRange GetConstBoundsMinValueRange() const override;
	UE_API virtual FPCGPointBoundsMax::ConstValueRange GetConstBoundsMaxValueRange() const override;
	UE_API virtual FPCGPointColor::ConstValueRange GetConstColorValueRange() const override;
	UE_API virtual FPCGPointSteepness::ConstValueRange GetConstSteepnessValueRange() const override;
	UE_API virtual FPCGPointSeed::ConstValueRange GetConstSeedValueRange() const override;
	UE_API virtual FPCGPointMetadataEntry::ConstValueRange GetConstMetadataEntryValueRange() const override;
	//~End UPCGBasePointData Interface
		
protected:
	UE_API virtual void InitializeSpatialDataInternal(const FPCGInitializeFromDataParams& InParams) override;

private:
	UE_API void FlattenPropertiesIfNeeded(EPCGPointNativeProperties Property = EPCGPointNativeProperties::All);

	template<EPCGPointNativeProperties Property>
	FPCGPointArrayProperty<typename TPCGPointNativeProperty<Property>::Type>* GetProperty(bool bWithInheritance = true)
	{
		if (bWithInheritance)
		{
			if (EnumHasAnyFlags(static_cast<EPCGPointNativeProperties>(InheritedProperties), TPCGPointNativeProperty<Property>::EnumValue))
			{
				check(ParentData);
				return ParentData->GetProperty<Property>(bWithInheritance);
			}
		}

		if constexpr (Property == EPCGPointNativeProperties::Transform)
		{
			return &PointArray.Transform;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Density)
		{
			return &PointArray.Density;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Steepness)
		{
			return &PointArray.Steepness;
		}
		else if constexpr (Property == EPCGPointNativeProperties::BoundsMin)
		{
			return &PointArray.BoundsMin;
		}
		else if constexpr (Property == EPCGPointNativeProperties::BoundsMax)
		{
			return &PointArray.BoundsMax;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Color)
		{
			return &PointArray.Color;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Seed)
		{
			return &PointArray.Seed;
		}
		else if constexpr (Property == EPCGPointNativeProperties::MetadataEntry)
		{
			return &PointArray.MetadataEntry;
		}
		else
		{
			static_assert(TPCGPointUnsupportedProperty<Property>::Value, "Unsupported EPCGPointNativeProperties value");
			return nullptr;
		}
	}

	template<EPCGPointNativeProperties Property>
	const FPCGPointArrayProperty<typename TPCGPointNativeProperty<Property>::Type>* GetProperty(bool bWithInheritance = true) const
	{
		if (bWithInheritance)
		{
			if (EnumHasAnyFlags(static_cast<EPCGPointNativeProperties>(InheritedProperties), TPCGPointNativeProperty<Property>::EnumValue))
			{
				check(ParentData);
				return ParentData->GetProperty<Property>(bWithInheritance);
			}
		}

		if constexpr (Property == EPCGPointNativeProperties::Transform)
		{
			return &PointArray.Transform;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Density)
		{
			return &PointArray.Density;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Steepness)
		{
			return &PointArray.Steepness;
		}
		else if constexpr (Property == EPCGPointNativeProperties::BoundsMin)
		{
			return &PointArray.BoundsMin;
		}
		else if constexpr (Property == EPCGPointNativeProperties::BoundsMax)
		{
			return &PointArray.BoundsMax;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Color)
		{
			return &PointArray.Color;
		}
		else if constexpr (Property == EPCGPointNativeProperties::Seed)
		{
			return &PointArray.Seed;
		}
		else if constexpr (Property == EPCGPointNativeProperties::MetadataEntry)
		{
			return &PointArray.MetadataEntry;
		}
		else
		{
			static_assert(TPCGPointUnsupportedProperty<Property>::Value, "Unsupported EPCGPointNativeProperties value");
			return nullptr;
		}
	}

	template<EPCGPointNativeProperties NativeProperty>
	bool FlattenPropertyIfNeeded()
	{
		using PropertyType = typename TPCGPointNativeProperty<NativeProperty>::Type;
		EPCGPointNativeProperties EnumValue = TPCGPointNativeProperty<NativeProperty>::EnumValue;
		EPCGPointNativeProperties InheritedPropertiesEnum = static_cast<EPCGPointNativeProperties>(InheritedProperties);

		if (!EnumHasAnyFlags(InheritedPropertiesEnum, EnumValue))
		{
			return false;
		}

		const FPCGPointArrayProperty<PropertyType>* InheritedProperty = GetProperty<NativeProperty>(/*bWithInheritance=*/true);
		FPCGPointArrayProperty<PropertyType>* Property = GetProperty<NativeProperty>(/*bWithInheritance=*/false);

		// Should not be equal since InheritedProperties contains that property
		if (ensure(InheritedProperty != Property))
		{
			if (InheritedProperty->IsAllocated())
			{
				Property->Allocate(/*bInitializeValues=*/false);
			}

			check(InheritedProperty->Num() == Property->Num());
			InheritedProperty->CopyTo(*Property, 0, 0, Property->Num());
		}

		EnumRemoveFlags(InheritedPropertiesEnum, EnumValue);
		InheritedProperties = static_cast<uint32>(InheritedPropertiesEnum);

		if (InheritedPropertiesEnum == EPCGPointNativeProperties::None)
		{
			ParentData = nullptr;
		}

		return true;
	};
	
	UPROPERTY()
	FPCGPointArray PointArray;

	UPROPERTY(VisibleAnywhere, Category = ParentData)
	TObjectPtr<UPCGPointArrayData> ParentData = nullptr;

	UPROPERTY(VisibleAnywhere, Category = ParentData, meta = (BitMask, BitmaskEnum = "/Script/PCG.EPCGPointNativeProperties"))
	uint32 InheritedProperties = 0;
};

#undef UE_API
