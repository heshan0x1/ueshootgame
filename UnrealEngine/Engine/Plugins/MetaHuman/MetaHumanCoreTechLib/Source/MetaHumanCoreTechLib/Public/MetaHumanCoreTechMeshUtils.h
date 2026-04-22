// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORETECHLIB_API

class IDNAReader;

namespace UE
{
namespace MetaHuman
{
	/**
	 * For each InInputUVs, find the closest UV from the InMeshUVs and return the index and distance in UV space from it
	* @param[in] InMeshUVs Mesh UVs to be searched within  
	* @param[in] InInputUVs Array of UVs. For each UV, finds the closest UV in InMeshUVs and returns it's index  
	* @returns Returns the index of corresponding UV in InMeshUVs and distance to UV for each input UV in InInputUVs
	 */
	UE_API TArray<TPair<int32, float>> GetClosestUVIndices(const TArray<FVector2f>& InMeshUVs, const TArray<FVector2f>& InInputUVs);

	UE_API TMap<int32, TArray<int32>> GetNeighbouringVertices(const TSharedPtr<IDNAReader>& InDNAReader, int32 InDNAMeshIndex, const TArray<int32>& InVertexIds);

	UE_API TArray<FVector3f> GetJointWorldTranslations(const TSharedPtr<IDNAReader>& InDNAReader);

}
}

#undef UE_API
