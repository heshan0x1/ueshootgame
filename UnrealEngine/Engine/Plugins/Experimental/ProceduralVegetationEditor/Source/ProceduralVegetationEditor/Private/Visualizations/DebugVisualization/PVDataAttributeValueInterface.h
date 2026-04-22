// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Helpers/PVUtilities.h"

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayTypes.h"

struct FManagedArrayCollection;

class FPVAttributeScalarValue;
class FPVAttributeValueInterface;

typedef std::shared_ptr<FPVAttributeValueInterface> FPVAttributeValuePtr;

class FPVAttributeValueInterface
{
public:
	virtual ~FPVAttributeValueInterface() = default;
	FPVAttributeValueInterface(const FName InGroupName, const FName InAttributeName, EManagedArrayType InAttributeType, const FManagedArrayCollection& InCollection) {}

	virtual TArray<FText> ToText() = 0;
	virtual TArray<FVector> ToVectors() = 0;
	virtual bool IsScalar() { return true; }

	static FPVAttributeValuePtr Create(const FName InGroupName, const FName InAttributeName, EManagedArrayType InAttributeType,const FManagedArrayCollection& InCollection);
};
