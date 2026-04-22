// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "MutableDataflowParameters.generated.h"

class USkeletalMesh;
class UTexture2D;
class UMaterialInterface;

USTRUCT()
struct FMutableParameterBase
{
	GENERATED_USTRUCT_BODY()

	virtual ~FMutableParameterBase() = default;
	
	UPROPERTY(EditAnywhere, Category = "Mutable")
	FString Name;
	
	bool operator== (const FMutableParameterBase& Other) const
	{
		return this->Name.ToLower() == Other.Name.ToLower();
	}
};


USTRUCT(BlueprintType, meta = (Experimental))
struct FMutableSkeletalMeshParameter : public FMutableParameterBase
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Mutable")
	TObjectPtr<USkeletalMesh> Mesh;
};


USTRUCT(BlueprintType, meta = (Experimental))
struct FMutableTextureParameter : public FMutableParameterBase
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Mutable")
	TObjectPtr<UTexture2D> Texture;
};


USTRUCT(BlueprintType, meta = (Experimental))
struct FMutableMaterialParameter : public FMutableParameterBase
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Mutable")
	TObjectPtr<UMaterialInterface> Material;
};