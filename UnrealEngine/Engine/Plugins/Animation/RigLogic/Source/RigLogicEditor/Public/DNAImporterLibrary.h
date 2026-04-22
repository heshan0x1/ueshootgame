// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DNAImporterLibrary.generated.h"

#define UE_API RIGLOGICEDITOR_API

/**
 * 
 */
UCLASS(MinimalAPI)
class UDNAImporterLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
	/** Import a DNA file onto the specified mesh asset.*/
	UFUNCTION(CallInEditor, BlueprintCallable, meta = (DisplayName = "Import Skeletal Mesh DNA File", Keywords = "DNA Importer Skeletal Mesh"), Category = "DNA")
	static UE_API void ImportSkeletalMeshDNA(const FString FileName, UObject* Mesh);
};

#undef UE_API
