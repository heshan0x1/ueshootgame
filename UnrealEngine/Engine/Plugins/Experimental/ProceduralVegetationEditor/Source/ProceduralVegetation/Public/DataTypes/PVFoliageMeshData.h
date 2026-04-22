// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PVData.h"

#include "PVFoliageMeshData.generated.h"

USTRUCT()
struct FPVDataTypeInfoFoliageMesh : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PROCEDURALVEGETATION_API);

#if WITH_EDITOR
	// If the type should be hidden to the user.
	virtual bool Hidden() const override { return true; };
#endif // WITH_EDITOR
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class UPVFoliageMeshData : public UPVData
{
	GENERATED_BODY()

public:
	//~ Begin UPCGData interface
	PCG_ASSIGN_DEFAULT_TYPE_INFO(FPVDataTypeInfoFoliageMesh)
	//~ End UPCGData interface
};