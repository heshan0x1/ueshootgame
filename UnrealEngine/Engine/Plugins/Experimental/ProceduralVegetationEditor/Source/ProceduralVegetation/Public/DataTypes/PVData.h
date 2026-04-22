// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "Data/PCGSpatialData.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PVUtilities.h"
#include "PVData.generated.h"

struct FPCGContext;

UENUM()
enum class EPVRenderType : uint8
{
	None 		= 0,
	PointData 	= 1 << 0,
	Mesh 		= 1 << 1,
	Foliage 	= 1 << 2,
	Bones 		= 1 << 3,
	FoliageGrid = 1 << 4,

	// Add new renter type above
	Count	
};

ENUM_CLASS_FLAGS(EPVRenderType)

UENUM()
enum class EPVDebugType
{
	Point,
	Branches,
	Foliage,
	Custom
};

UENUM()
enum class EPVDebugValueVisualizationMode
{
	Text,
	Direction,
	Point
};

USTRUCT()
struct FPVVisualizationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	EPVDebugType DebugType = EPVDebugType::Point;

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	bool bShowAnchorPoints = true;

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	FName AttributeToFilter;

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	EPVDebugValueVisualizationMode VisualizationMode = EPVDebugValueVisualizationMode::Text;

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	FColor Color = FColor::Black;
	
	FName GetDebugTypeString() const
	{
		switch (DebugType)
		{
		case EPVDebugType::Point:
			return "Points";

		case EPVDebugType::Branches:
			return "Primitives";

		case EPVDebugType::Foliage:
			return "Foliage";

		case EPVDebugType::Custom:
			return "Custom";
		}
		return NAME_None;
	}
};

USTRUCT()
struct FPVDebugSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DebugSettings)
	TArray<FPVVisualizationSettings> VisualizationSettings;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PROCEDURALVEGETATION_API UPVData : public UPCGSpatialData
{
	GENERATED_BODY()

public:
	UPVData(const FObjectInitializer& ObjectInitializer);
	
	void Initialize(FManagedArrayCollection&& InCollection, bool bCanTakeOwnership = false);
	
	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::Other; }
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	// ~End UPCGData interface

	// ~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 3; }
	virtual FBox GetBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	//~End UPCGSpatialData interface

	const FManagedArrayCollection& GetCollection() const { return Collection; }
	FManagedArrayCollection& GetCollection() { return Collection; }
	
#if WITH_EDITOR
	void SetDebugSettings(const FPVDebugSettings& InSettings) const { DebugSettings = InSettings; }
	const FPVDebugSettings& GetDebugSettings() const { return DebugSettings; }
#endif
	
protected:

	FManagedArrayCollection Collection;

#if WITH_EDITOR
	mutable FPVDebugSettings DebugSettings;
#endif
	
	// ~Begin UPCGData interface
	virtual bool SupportsFullDataCrc() const override { return true; }
	// ~End UPCGData interface

	// ~Begin UPCGSpatialData interface
	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
public:
	virtual const UPCGPointData* ToPointData(FPCGContext* Context, const FBox& InBounds) const override;
	virtual const UPCGPointArrayData* ToPointArrayData(FPCGContext* Context, const FBox& InBounds) const override;
	//~End UPCGSpatialData interface
	
private:
	const UPCGBasePointData* ToBasePointData(FPCGContext* Context, const FBox& InBounds, TSubclassOf<UPCGBasePointData> PointDataClass) const;
protected:
	
	mutable FBox CachedBounds = FBox(EForceInit::ForceInit);
};
