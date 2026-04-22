// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDataAttributeValueInterface.h"

#include "GeometryCollection/ManagedArrayCollection.h"
#include "PVDataAttributeValue.h"

FPVAttributeValuePtr FPVAttributeValueInterface::Create(const FName InGroupName, const FName InAttributeName,
	EManagedArrayType InAttributeType, const FManagedArrayCollection& InCollection)
{
	switch (InAttributeType)
	{
	case EManagedArrayType::FInt32Type:
	case EManagedArrayType::FFloatType:
			return std::make_shared<FPVAttributeScalarValue>(InGroupName, InAttributeName, InAttributeType,InCollection);
	case EManagedArrayType::FVectorType:
			return std::make_shared<FPVAttributeVectorValue>(InGroupName, InAttributeName, InAttributeType,InCollection);
	default:
		return nullptr;
	}
}