// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hash/CityHash.h"
#include "NaniteEncodeShared.h"

namespace Nanite
{

// Wasteful to store size for every vert but easier this way.
struct FVariableVertex
{
	const float*	Data;
	uint32			SizeInBytes;

	bool operator==( FVariableVertex Other ) const
	{
		return 0 == FMemory::Memcmp( Data, Other.Data, SizeInBytes );
	}
};

FORCEINLINE uint32 GetTypeHash( FVariableVertex Vert )
{
	return CityHash32( (const char*)Vert.Data, Vert.SizeInBytes );
}

struct FVertexMapEntry
{
	uint32 LocalClusterIndex;
	uint32 VertexIndex;
};

void CalculateEncodingInfos(
	TArray<FEncodingInfo>& EncodingInfos,
	const TArray<FCluster>& Clusters,
	int32 NormalPrecision,
	int32 TangentPrecision,
	int32 BoneWeightPrecision
);

void EncodeGeometryData(
	const uint32 LocalClusterIndex,
	const FCluster& Cluster,
	const FEncodingInfo& EncodingInfo,
	const TArrayView<uint16> PageDependencies,
	const TArray<TMap<FVariableVertex, FVertexMapEntry>>& PageVertexMaps,
	TMap<FVariableVertex, uint32>& UniqueVertices, uint32& NumCodedVertices,
	FPageStreams& Streams);

TArray<TMap<FVariableVertex, FVertexMapEntry>> BuildVertexMaps(
	const TArray<FPage>& Pages,
	const TArray<FCluster>& Clusters,
	const TArray<FClusterGroupPart>& Parts);

}
