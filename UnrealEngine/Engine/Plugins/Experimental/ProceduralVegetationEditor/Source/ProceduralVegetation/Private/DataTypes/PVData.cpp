// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataTypes/PVData.h"
#include "PCGContext.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGHelpers.h"
#include "Serialization/ArchiveCrc32.h"
#include "UObject/Package.h"

UPVData::UPVData(const FObjectInitializer& ObjectInitializer)
	: UPCGSpatialData(ObjectInitializer)
{
	
}

void UPVData::Initialize(FManagedArrayCollection&& InCollection, bool bCanTakeOwnership)
{
	Collection = InCollection;
}

void UPVData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	if (bFullDataCrc)
	{
		// Implementation note: metadata not supported at this point.

		FString ClassName = StaticClass()->GetPathName();

		Ar << ClassName;
		Ar << Collection;
#if WITH_EDITOR
		Ar << DebugSettings;
#endif
	}
	else
	{
		AddUIDToCrc(Ar);
	}
}

FBox UPVData::GetBounds() const
{
	return CachedBounds;
}

bool UPVData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	return false;
}

const UPCGBasePointData* UPVData::ToBasePointData(FPCGContext* Context, const FBox& InBounds, TSubclassOf<UPCGBasePointData> PointDataClass) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPrimitiveData::CreatePointData);

	UPCGBasePointData* Data = FPCGContext::NewObject_AnyThread<UPCGBasePointData>(Context, GetTransientPackage(), PointDataClass);
	return Data;
}

UPCGSpatialData* UPVData::CopyInternal(FPCGContext* Context) const
{
	UPVData* NewProceduralVegetationData = FPCGContext::NewObject_AnyThread<UPVData>(Context);

	FManagedArrayCollection NewCollection;
	Collection.CopyTo(&NewCollection);
	
	NewProceduralVegetationData->Initialize(MoveTemp(NewCollection), /*bCanTakeOwnership=*/false);

#if WITH_EDITOR
	NewProceduralVegetationData->DebugSettings = DebugSettings;
#endif
	
	return NewProceduralVegetationData;
}

const UPCGPointArrayData* UPVData::ToPointArrayData(FPCGContext* Context, const FBox& InBounds) const
{
	return CastChecked<UPCGPointArrayData>(ToBasePointData(Context, InBounds, UPCGPointArrayData::StaticClass()));
}
const UPCGPointData* UPVData::ToPointData(FPCGContext* Context, const FBox& InBounds) const
{
	return CastChecked<UPCGPointData>(ToBasePointData(Context, InBounds, UPCGPointData::StaticClass()));
}

