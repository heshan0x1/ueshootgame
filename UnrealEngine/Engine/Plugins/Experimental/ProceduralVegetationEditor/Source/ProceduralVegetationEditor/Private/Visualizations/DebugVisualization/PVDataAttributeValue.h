// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Helpers/PVUtilities.h"

#include "PVDataAttributeValueInterface.h"

class FPVAttributeScalarValue : public FPVAttributeValueInterface
{
public:
	FPVAttributeScalarValue(const FName InGroupName, const FName InAttributeName, EManagedArrayType InAttributeType, const FManagedArrayCollection& InCollection);
	virtual TArray<FText> ToText() override;
	virtual TArray<FVector> ToVectors() override;

private:
	TArray<float> ValueArray;
};

class FPVAttributeVectorValue : public FPVAttributeValueInterface
{
public:
	FPVAttributeVectorValue(const FName InGroupName, const FName InAttributeName, EManagedArrayType InAttributeType, const FManagedArrayCollection& InCollection);
	virtual TArray<FText> ToText() override;
	virtual bool IsScalar() override { return false;}
	virtual TArray<FVector> ToVectors() override;

private:
	TArray<FVector> ValueArray;
};
