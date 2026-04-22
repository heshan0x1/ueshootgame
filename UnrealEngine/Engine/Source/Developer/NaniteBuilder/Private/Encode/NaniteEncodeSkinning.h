// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NaniteDefinitions.h"
#include "Rendering/NaniteResources.h"
#include "NaniteEncodeShared.h"

namespace Nanite
{

class FCluster;

struct FPackedBoneInfluenceHeader
{
	uint32	DataOffset_VertexInfluences = 0u;						// DataOffset: 22, NumVertexInfluences: 8
	uint32	NumVertexBoneIndexBits_NumVertexBoneWeightBits = 0u;	// NumVertexBoneIndexBits: 6, NumVertexBoneWeightBits: 5

	void	SetDataOffset(uint32 Offset)				{ SetBits(DataOffset_VertexInfluences, Offset, 22,  0); }
	void	SetNumVertexInfluences(uint32 Num)			{ SetBits(DataOffset_VertexInfluences,    Num, 10, 22); }
	void	SetNumVertexBoneIndexBits(uint32 NumBits)	{ SetBits(NumVertexBoneIndexBits_NumVertexBoneWeightBits, NumBits, 6,  0); }
	void	SetNumVertexBoneWeightBits(uint32 NumBits)	{ SetBits(NumVertexBoneIndexBits_NumVertexBoneWeightBits, NumBits, 5,  6); }
};

void QuantizeBoneWeights(TArray<FCluster>& Clusters, int32 BoneWeightPrecision);

void QuantizeAndSortBoneInfluenceWeights(TArrayView<FVector2f> BoneInfluences, uint32 TargetTotalQuatizedWeight);

void CalculateInfluences(FBoneInfluenceInfo& InfluenceInfo, const FCluster& Cluster, int32 BoneWeightPrecision);
void PackBoneInfluenceHeader(FPackedBoneInfluenceHeader& PackedBoneInfluenceHeader, const FBoneInfluenceInfo& BoneInfluenceInfo);

}
