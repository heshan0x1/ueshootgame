// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDataAttributeValue.h"

#include "GeometryCollection/ManagedArrayCollection.h"

FPVAttributeScalarValue::FPVAttributeScalarValue(const FName InGroupName, const FName InAttributeName, EManagedArrayType InAttributeType,
                                                               const FManagedArrayCollection& InCollection)
	: FPVAttributeValueInterface(InGroupName, InAttributeName, InAttributeType, InCollection)
{
	if (InCollection.HasAttribute(InAttributeName, InGroupName))
	{
		if (InAttributeType == EManagedArrayType::FInt32Type)
		{
			const TManagedArray<int>& FloatAttributesArray = InCollection.GetAttribute<int>(InAttributeName, InGroupName);

			for (int i = 0; i < FloatAttributesArray.Num(); i++)
			{
				ValueArray.Add(FloatAttributesArray[i]);
			}
		}
		else if (InAttributeType == EManagedArrayType::FFloatType)
		{
			const TManagedArray<float>& FloatAttributesArray = InCollection.GetAttribute<float>(InAttributeName, InGroupName);

			for (int i = 0; i < FloatAttributesArray.Num(); i++)
			{
				ValueArray.Add(FloatAttributesArray[i]);
			}	
		}
		else
		{
			check(false);
		}
	}
}

TArray<FText> FPVAttributeScalarValue::ToText()
{
	TArray<FText> TextArray;
	
	for (int i = 0; i < ValueArray.Num(); i++)
	{
		TextArray.Add(FText::AsNumber(ValueArray[i]));
	}

	return TextArray;
}

TArray<FVector> FPVAttributeScalarValue::ToVectors()
{
	static TArray<FVector> VecArray;
	return VecArray;
}

FPVAttributeVectorValue::FPVAttributeVectorValue(const FName InGroupName, const FName InAttributeName,
                                                               EManagedArrayType InAttributeType, const FManagedArrayCollection& InCollection)
: FPVAttributeValueInterface(InGroupName, InAttributeName, InAttributeType, InCollection)
{
	if (InCollection.HasAttribute(InAttributeName, InGroupName))
	{
		if (InAttributeType == EManagedArrayType::FVectorType)
		{
			const TManagedArray<FVector3f>& FloatAttributesArray = InCollection.GetAttribute<FVector3f>(InAttributeName, InGroupName);

			for (int i = 0; i < FloatAttributesArray.Num(); i++)
			{
				ValueArray.Add(FVector(FloatAttributesArray[i].X, FloatAttributesArray[i].Y, FloatAttributesArray[i].Z));
			}
		}
		else
		{
			check(false);
		}
	}
}

TArray<FText> FPVAttributeVectorValue::ToText()
{
	TArray<FText> TextArray;
	
	for (int i = 0; i < ValueArray.Num(); i++)
	{
		TextArray.Add(ValueArray[i].ToText());
	}

	return TextArray;
}

TArray<FVector> FPVAttributeVectorValue::ToVectors()
{
	TArray<FVector> Vectors;
	
	for (int i = 0; i < ValueArray.Num(); i++)
	{
		Vectors.Add(ValueArray[i]);
	}

	return Vectors;
}
