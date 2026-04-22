// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeMaterial.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteDefinitions.h"
#include "Async/ParallelFor.h"

namespace Nanite
{

static uint32 PackMaterialTableRange(uint32 TriStart, uint32 TriLength, uint32 MaterialIndex)
{
	uint32 Packed = 0x00000000;
	// uint32 TriStart      :  8; // max 128 triangles
	// uint32 TriLength     :  8; // max 128 triangles
	// uint32 MaterialIndex :  6; // max  64 materials
	// uint32 Padding       : 10;
	check(TriStart <= 128);
	check(TriLength <= 128);
	check(MaterialIndex < 64);
	Packed |= TriStart;
	Packed |= TriLength << 8;
	Packed |= MaterialIndex << 16;
	return Packed;
}

static uint32 PackMaterialFastPath(uint32 Material0Length, uint32 Material0Index, uint32 Material1Length, uint32 Material1Index, uint32 Material2Index)
{
	uint32 Packed = 0x00000000;
	// Material Packed Range - Fast Path (32 bits)
	// uint Material0Index  : 6;   // max  64 materials (0:Material0Length)
	// uint Material1Index  : 6;   // max  64 materials (Material0Length:Material1Length)
	// uint Material2Index  : 6;   // max  64 materials (remainder)
	// uint Material0Length : 7;   // max 128 triangles (num minus one)
	// uint Material1Length : 7;   // max  64 triangles (materials are sorted, so at most 128/2)
	check(Material0Index  <  64);
	check(Material1Index  <  64);
	check(Material2Index  <  64);
	check(Material0Length >= 1);
	check(Material0Length <= 128);
	check(Material1Length <= 64);
	check(Material1Length <= Material0Length);
	Packed |= Material0Index;
	Packed |= Material1Index << 6;
	Packed |= Material2Index << 12;
	Packed |= (Material0Length - 1u) << 18;
	Packed |= Material1Length << 25;
	return Packed;
}

static uint32 PackMaterialSlowPath(uint32 MaterialTableOffset, uint32 MaterialTableLength)
{
	// Material Packed Range - Slow Path (32 bits)
	// uint BufferIndex     : 19; // 2^19 max value (tons, it's per prim)
	// uint BufferLength	: 6;  // max 64 materials, so also at most 64 ranges (num minus one)
	// uint Padding			: 7;  // always 127 for slow path. corresponds to Material1Length=127 in fast path
	check(MaterialTableOffset < 524288); // 2^19 - 1
	check(MaterialTableLength > 0); // clusters with 0 materials use fast path
	check(MaterialTableLength <= 64);
	uint32 Packed = MaterialTableOffset;
	Packed |= (MaterialTableLength - 1u) << 19;
	Packed |= (0xFE000000u);
	return Packed;
}

uint32 CalcMaterialTableSize( const FCluster& Cluster )
{
	const uint32 NumMaterials = Cluster.MaterialRanges.Num();
	return NumMaterials > 3 ? NumMaterials : 0;
}

// Prints material range stats. This has to happen separate from BuildMaterialRanges as materials might be recalculated because of cluster splitting.
void PrintMaterialRangeStats( const TArray<FCluster>& Clusters )
{
	TBitArray<> UsedMaterialIndices(false, NANITE_MAX_CLUSTER_MATERIALS);

	uint32 NumClusterMaterials[ 4 ] = { 0, 0, 0, 0 }; // 1, 2, 3, >= 4

	const uint32 NumClusters = Clusters.Num();
	for( uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++ )
	{
		const FCluster& Cluster = Clusters[ ClusterIndex ];

		// TODO: Valid assumption? All null materials should have been assigned default material at this point.
		check( Cluster.MaterialRanges.Num() > 0 );
		NumClusterMaterials[ FMath::Min( Cluster.MaterialRanges.Num() - 1, 3 ) ]++;

		for( const FMaterialRange& MaterialRange : Cluster.MaterialRanges )
		{
			UsedMaterialIndices[ MaterialRange.MaterialIndex ] = true;
		}
	}

	UE_LOG( LogStaticMesh, Log, TEXT( "Material Stats - Unique Materials: %d, Fast Path Clusters: %d, Slow Path Clusters: %d, 1 Material: %d, 2 Materials: %d, 3 Materials: %d, At Least 4 Materials: %d" ),
		UsedMaterialIndices.CountSetBits(), Clusters.Num() - NumClusterMaterials[ 3 ], NumClusterMaterials[ 3 ], NumClusterMaterials[ 0 ], NumClusterMaterials[ 1 ], NumClusterMaterials[ 2 ], NumClusterMaterials[ 3 ] );

#if 0
	for( uint32 MaterialIndex = 0; MaterialIndex < MAX_CLUSTER_MATERIALS; ++MaterialIndex )
	{
		if( UsedMaterialIndices[ MaterialIndex ] )
		{
			UE_LOG( LogStaticMesh, Log, TEXT( "  Material Index: %d" ), MaterialIndex );
		}
	}
#endif
}

uint32 PackMaterialInfo(const FCluster& Cluster, TArray<uint32>& OutMaterialTable, uint32 MaterialTableStartOffset)
{
	// Encode material ranges
	uint32 NumMaterialTriangles = 0;
	for (int32 RangeIndex = 0; RangeIndex < Cluster.MaterialRanges.Num(); ++RangeIndex)
	{
		check(Cluster.MaterialRanges[RangeIndex].RangeLength <= 128);
		check(Cluster.MaterialRanges[RangeIndex].RangeLength > 0);
		check(Cluster.MaterialRanges[RangeIndex].MaterialIndex < NANITE_MAX_CLUSTER_MATERIALS);
		NumMaterialTriangles += Cluster.MaterialRanges[RangeIndex].RangeLength;
	}

	// All triangles accounted for in material ranges?
	check(NumMaterialTriangles == Cluster.MaterialIndexes.Num());

	uint32 PackedMaterialInfo = 0x00000000;

	// The fast inline path can encode up to 3 materials
	if (Cluster.MaterialRanges.Num() <= 3)
	{
		uint32 Material0Length = 0;
		uint32 Material0Index = 0;
		uint32 Material1Length = 0;
		uint32 Material1Index = 0;
		uint32 Material2Index = 0;

		if (Cluster.MaterialRanges.Num() > 0)
		{
			const FMaterialRange& Material0 = Cluster.MaterialRanges[0];
			check(Material0.RangeStart == 0);
			Material0Length = Material0.RangeLength;
			Material0Index = Material0.MaterialIndex;
		}

		if (Cluster.MaterialRanges.Num() > 1)
		{
			const FMaterialRange& Material1 = Cluster.MaterialRanges[1];
			check(Material1.RangeStart == Cluster.MaterialRanges[0].RangeLength);
			Material1Length = Material1.RangeLength;
			Material1Index = Material1.MaterialIndex;
		}

		if (Cluster.MaterialRanges.Num() > 2)
		{
			const FMaterialRange& Material2 = Cluster.MaterialRanges[2];
			check(Material2.RangeStart == Material0Length + Material1Length);
			check(Material2.RangeLength == Cluster.MaterialIndexes.Num() - Material0Length - Material1Length);
			Material2Index = Material2.MaterialIndex;
		}

		PackedMaterialInfo = PackMaterialFastPath(Material0Length, Material0Index, Material1Length, Material1Index, Material2Index);
	}
	// Slow global table search path
	else
	{
		uint32 MaterialTableOffset = OutMaterialTable.Num() + MaterialTableStartOffset;
		uint32 MaterialTableLength = Cluster.MaterialRanges.Num();
		check(MaterialTableLength > 0);

		for (int32 RangeIndex = 0; RangeIndex < Cluster.MaterialRanges.Num(); ++RangeIndex)
		{
			const FMaterialRange& Material = Cluster.MaterialRanges[RangeIndex];
			OutMaterialTable.Add(PackMaterialTableRange(Material.RangeStart, Material.RangeLength, Material.MaterialIndex));
		}

		PackedMaterialInfo = PackMaterialSlowPath(MaterialTableOffset, MaterialTableLength);
	}

	return PackedMaterialInfo;
}

// Sort cluster triangles into material ranges. Add Material ranges to clusters.
void BuildMaterialRanges( TArray<FCluster>& Clusters )
{
	ParallelFor(TEXT("NaniteEncode.BuildMaterialRanges.PF"), Clusters.Num(), 256,
		[&]( uint32 ClusterIndex )
		{
			Clusters[ ClusterIndex ].BuildMaterialRanges();
		} );
}

} // namespace Nanite
