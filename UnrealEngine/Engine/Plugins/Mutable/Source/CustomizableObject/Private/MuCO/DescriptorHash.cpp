// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/DescriptorHash.h"

#include "MuCO/CustomizableObject.h"


FDescriptorHash::FDescriptorHash(const FCustomizableObjectInstanceDescriptor& Descriptor)
{
#if WITH_EDITORONLY_DATA
	if (Descriptor.CustomizableObject)
	{
		Hash = HashCombine(Hash, GetTypeHash(Descriptor.CustomizableObject->GetPathName()));
	}
#endif

	for (const FCustomizableObjectBoolParameterValue& Value : Descriptor.BoolParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}	

	for (const FCustomizableObjectIntParameterValue& Value : Descriptor.IntParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}
	
	for (const FCustomizableObjectFloatParameterValue& Value : Descriptor.FloatParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}

	for (const FCustomizableObjectTextureParameterValue& Value : Descriptor.TextureParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}

	for (const FCustomizableObjectSkeletalMeshParameterValue& Value : Descriptor.SkeletalMeshParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}

	for (const FCustomizableObjectVectorParameterValue& Value : Descriptor.VectorParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}

	for (const FCustomizableObjectProjectorParameterValue& Value : Descriptor.ProjectorParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}
	
	for (const FCustomizableObjectTransformParameterValue& Value : Descriptor.TransformParameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Value));
	}

	Hash = HashCombine(Hash, GetTypeHash(Descriptor.State));
	Hash = HashCombine(Hash, GetTypeHash(Descriptor.GetBuildParameterRelevancy()));
	
	bStreamingEnabled = Descriptor.bStreamingEnabled;
	MinLODs = Descriptor.MinLOD;
	QualitySettingMinLODs = Descriptor.QualitySettingMinLODs;
	FirstRequestedLOD = Descriptor.FirstRequestedLOD;
}


bool FDescriptorHash::IsSubset(const FDescriptorHash& Other) const
{
	if (Hash != Other.Hash)
	{
		return false;
	}

	if (bStreamingEnabled && Other.bStreamingEnabled)
	{
		return true;
	}

	for (const TTuple<FName, uint8>& Pair : MinLODs)
	{
		const uint8* Result = Other.MinLODs.Find(Pair.Key);
		if (!Result)
		{
			return false;
		}

		if (Pair.Value < *Result)
		{
			return false;
		}
	}
	
	// Scalability quality change : Do not care if it is a subset, always update
	for (const TTuple<FName, uint8>& Pair : QualitySettingMinLODs)
	{
		const uint8* Result = Other.QualitySettingMinLODs.Find(Pair.Key);
		if (!Result)
		{
			return false;
		}

		if (Pair.Value != *Result)
		{
			return false;
		}
	}

	if (FirstRequestedLOD.Num() != Other.FirstRequestedLOD.Num())
	{
		return false;
	}
	
	for (const TTuple<FName, uint8>& Pair : FirstRequestedLOD)
	{
		const uint8* PairOther = Other.FirstRequestedLOD.Find(Pair.Key);
		if (!PairOther)
		{
			return false;
		}

		if (Pair.Value < *PairOther)
		{
			return false;
		}
	}

	return true;
}


FString FDescriptorHash::ToString() const
{
	TStringBuilder<150> Builder;

	Builder.Appendf(TEXT("(Hash=%u,"), Hash);
	
	Builder.Appendf(TEXT("MinLODs=["));
	for (const TTuple<FName, uint8>& MinLOD : MinLODs)
	{
		Builder.Appendf(TEXT("(%s, %i),"), *MinLOD.Key.ToString(), MinLOD.Value);
	}
	Builder.Appendf(TEXT("])"));

	Builder.Appendf(TEXT("FirstLODAvailable=["));
	for (const TTuple<FName, uint8>& QualitySetting : QualitySettingMinLODs)
	{
		Builder.Appendf(TEXT("(%s, %i),"), *QualitySetting.Key.ToString(), QualitySetting.Value);
	}
	Builder.Appendf(TEXT("])"));

	Builder.Appendf(TEXT("FirstRequestedLOD=["));
	for (const TTuple<FName, uint8>& RequestedLODs : FirstRequestedLOD)
	{
		Builder.Appendf(TEXT("(%s, %i),"), *RequestedLODs.Key.ToString(), RequestedLODs.Value);
	}
	Builder.Appendf(TEXT("])"));

	return Builder.ToString();
}
