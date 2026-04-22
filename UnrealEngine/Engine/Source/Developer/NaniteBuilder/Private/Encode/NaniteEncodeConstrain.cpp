// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeConstrain.h"

#include "Math/UnrealMath.h"
#include "Async/ParallelFor.h"

#include "Cluster.h"
#include "ClusterDAG.h"
#include "NaniteDefinitions.h"
#include "NaniteEncodeTriStrip.h"

namespace Nanite
{

// Weights for individual cache entries based on simulated annealing optimization on DemoLevel.
static int16 CacheWeightTable[NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE] =
{
	 577,	 616,	 641,  512,		 614,  635,  478,  651,
	  65,	 213,	 719,  490,		 213,  726,  863,  745,
	 172,	 939,	 805,  885,		 958, 1208, 1319, 1318,
	1475,	1779,	2342,  159,		2307, 1998, 1211,  932
};

// Constrain cluster to only use vertex references that are within a fixed sized trailing window from the current highest encountered vertex index.
// Triangles are reordered based on a FIFO-style cache optimization to minimize the number of vertices that need to be duplicated.
static void ConstrainClusterFIFO( FCluster& Cluster )
{
	uint32 NumOldTriangles = Cluster.NumTris;
	uint32 NumOldVertices = Cluster.Verts.Num();

	const uint32 MAX_CLUSTER_TRIANGLES_IN_DWORDS = (NANITE_MAX_CLUSTER_TRIANGLES + 31 ) / 32;

	uint32 VertexToTriangleMasks[NANITE_MAX_CLUSTER_TRIANGLES * 3][MAX_CLUSTER_TRIANGLES_IN_DWORDS] = {};

	// Generate vertex to triangle masks
	for( uint32 i = 0; i < NumOldTriangles; i++ )
	{
		uint32 i0 = Cluster.Indexes[ i * 3 + 0 ];
		uint32 i1 = Cluster.Indexes[ i * 3 + 1 ];
		uint32 i2 = Cluster.Indexes[ i * 3 + 2 ];
		check( i0 != i1 && i1 != i2 && i2 != i0 ); // Degenerate input triangle!

		VertexToTriangleMasks[ i0 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i1 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i2 ][ i >> 5 ] |= 1 << ( i & 31 );
	}

	uint32 TrianglesEnabled[ MAX_CLUSTER_TRIANGLES_IN_DWORDS ] = {};	// Enabled triangles are in the current material range and have not yet been visited.
	uint32 TrianglesTouched[ MAX_CLUSTER_TRIANGLES_IN_DWORDS ] = {};	// Touched triangles have had at least one of their vertices visited.

	uint16 OptimizedIndices[NANITE_MAX_CLUSTER_TRIANGLES * 3 ];

	uint32 NumNewVertices = 0;
	uint32 NumNewTriangles = 0;
	uint16 OldToNewVertex[NANITE_MAX_CLUSTER_TRIANGLES * 3];
	uint16 NewToOldVertex[NANITE_MAX_CLUSTER_TRIANGLES * 3] = {};	// Initialize to make static analysis happy
	FMemory::Memset( OldToNewVertex, -1, sizeof( OldToNewVertex ) );

	auto ScoreVertex = [ &OldToNewVertex, &NumNewVertices ] ( uint32 OldVertex )
	{
		uint16 NewIndex = OldToNewVertex[ OldVertex ];

		int32 CacheScore = 0;
		if( NewIndex != 0xFFFF )
		{
			uint32 CachePosition = ( NumNewVertices - 1 ) - NewIndex;
			if( CachePosition < NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE )
				CacheScore = CacheWeightTable[ CachePosition ];
		}

		return CacheScore;
	};

	uint32 RangeStart = 0;
	for( FMaterialRange& MaterialRange : Cluster.MaterialRanges )
	{
		check( RangeStart == MaterialRange.RangeStart );
		uint32 RangeLength = MaterialRange.RangeLength;

		// Enable triangles from current range
		for( uint32 i = 0; i < MAX_CLUSTER_TRIANGLES_IN_DWORDS; i++ )
		{
			int32 RangeStartRelativeToDword = (int32)RangeStart - (int32)i * 32;
			int32 BitStart = FMath::Max( RangeStartRelativeToDword, 0 );
			int32 BitEnd = FMath::Max( RangeStartRelativeToDword + (int32)RangeLength, 0 );
			uint32 StartMask = BitStart < 32 ? ( ( 1u << BitStart ) - 1u ) : 0xFFFFFFFFu;
			uint32 EndMask = BitEnd < 32 ? ( ( 1u << BitEnd ) - 1u ) : 0xFFFFFFFFu;
			TrianglesEnabled[ i ] |= StartMask ^ EndMask;
		}

		while( true )
		{
			uint32 NextTriangleIndex = 0xFFFF;
			int32 NextTriangleScore = 0;

			// Pick highest scoring available triangle
			for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MAX_CLUSTER_TRIANGLES_IN_DWORDS; TriangleDwordIndex++ )
			{
				uint32 CandidateMask = TrianglesTouched[ TriangleDwordIndex ] & TrianglesEnabled[ TriangleDwordIndex ];
				while( CandidateMask )
				{
					uint32 TriangleDwordOffset = FMath::CountTrailingZeros( CandidateMask );
					CandidateMask &= CandidateMask - 1;

					int32 TriangleIndex = ( TriangleDwordIndex << 5 ) + TriangleDwordOffset;

					int32 TriangleScore = 0;
					TriangleScore += ScoreVertex( Cluster.Indexes[ TriangleIndex * 3 + 0 ] );
					TriangleScore += ScoreVertex( Cluster.Indexes[ TriangleIndex * 3 + 1 ] );
					TriangleScore += ScoreVertex( Cluster.Indexes[ TriangleIndex * 3 + 2 ] );

					if( TriangleScore > NextTriangleScore )
					{
						NextTriangleIndex = TriangleIndex;
						NextTriangleScore = TriangleScore;
					}
				}
			}

			if( NextTriangleIndex == 0xFFFF )
			{
				// If we didn't find a triangle. It might be because it is part of a separate component. Look for an unvisited triangle to restart from.
				for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MAX_CLUSTER_TRIANGLES_IN_DWORDS; TriangleDwordIndex++ )
				{
					uint32 EnableMask = TrianglesEnabled[ TriangleDwordIndex ];
					if( EnableMask )
					{
						NextTriangleIndex = ( TriangleDwordIndex << 5 ) + FMath::CountTrailingZeros( EnableMask );
						break;
					}
				}

				if( NextTriangleIndex == 0xFFFF )
					break;
			}

			uint32 OldIndex0 = Cluster.Indexes[ NextTriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Cluster.Indexes[ NextTriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Cluster.Indexes[ NextTriangleIndex * 3 + 2 ];

			// Mark incident triangles
			for( uint32 i = 0; i < MAX_CLUSTER_TRIANGLES_IN_DWORDS; i++ )
			{
				TrianglesTouched[ i ] |= VertexToTriangleMasks[ OldIndex0 ][ i ] | VertexToTriangleMasks[ OldIndex1 ][ i ] | VertexToTriangleMasks[ OldIndex2 ][ i ];
			}

			uint16& NewIndex0 = OldToNewVertex[OldIndex0];
			uint16& NewIndex1 = OldToNewVertex[OldIndex1];
			uint16& NewIndex2 = OldToNewVertex[OldIndex2];

			// Generate new indices such that they are all within a trailing window of NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE of NumNewVertices.
			// This can require multiple iterations as new/duplicate vertices can push other vertices outside the window.			
			uint32 TestNumNewVertices = NumNewVertices;
			TestNumNewVertices += (NewIndex0 == 0xFFFF) + (NewIndex1 == 0xFFFF) + (NewIndex2 == 0xFFFF);

			while(true)
			{
				if (NewIndex0 != 0xFFFF && TestNumNewVertices - NewIndex0 >= NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE)
				{
					NewIndex0 = 0xFFFF;
					TestNumNewVertices++;
					continue;
				}

				if (NewIndex1 != 0xFFFF && TestNumNewVertices - NewIndex1 >= NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE)
				{
					NewIndex1 = 0xFFFF;
					TestNumNewVertices++;
					continue;
				}

				if (NewIndex2 != 0xFFFF && TestNumNewVertices - NewIndex2 >= NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE)
				{
					NewIndex2 = 0xFFFF;
					TestNumNewVertices++;
					continue;
				}
				break;
			}

			if (NewIndex0 == 0xFFFF) { NewIndex0 = uint16(NumNewVertices++); }
			if (NewIndex1 == 0xFFFF) { NewIndex1 = uint16(NumNewVertices++); }
			if (NewIndex2 == 0xFFFF) { NewIndex2 = uint16(NumNewVertices++); }
			NewToOldVertex[NewIndex0] = uint16(OldIndex0);
			NewToOldVertex[NewIndex1] = uint16(OldIndex1);
			NewToOldVertex[NewIndex2] = uint16(OldIndex2);

			// Output triangle
			OptimizedIndices[ NumNewTriangles * 3 + 0 ] = NewIndex0;
			OptimizedIndices[ NumNewTriangles * 3 + 1 ] = NewIndex1;
			OptimizedIndices[ NumNewTriangles * 3 + 2 ] = NewIndex2;
			NumNewTriangles++;

			// Disable selected triangle
			TrianglesEnabled[ NextTriangleIndex >> 5 ] &= ~( 1u << ( NextTriangleIndex & 31u ) );
		}
		RangeStart += RangeLength;
	}

	check( NumNewTriangles == NumOldTriangles );

	// Write back new triangle order
	for( uint32 i = 0; i < NumNewTriangles * 3; i++ )
	{
		Cluster.Indexes[ i ] = OptimizedIndices[ i ];
	}

	// Write back new vertex order including possibly duplicates
	FVertexArray OldVertices( Cluster.Verts.Format );
	Swap( OldVertices, Cluster.Verts );

	Cluster.Verts.Reserve( NumNewVertices );
	for( uint32 i = 0; i < NumNewVertices; i++ )
	{
		Cluster.Verts.Add( &OldVertices.GetPosition( NewToOldVertex[i] ) );
	}
}

static void BuildClusterFromClusterTriangleRange( const FCluster& InCluster, FCluster& OutCluster, uint32 StartTriangle, uint32 NumTriangles )
{
	OutCluster = InCluster;
	OutCluster.Indexes.Empty();
	OutCluster.MaterialIndexes.Empty();
	OutCluster.MaterialRanges.Empty();

	// Copy triangle indices and material indices.
	// Ignore that some of the vertices will no longer be referenced as that will be cleaned up in ConstrainCluster* pass
	OutCluster.Indexes.SetNumUninitialized( NumTriangles * 3 );
	OutCluster.MaterialIndexes.SetNumUninitialized( NumTriangles );
	for( uint32 i = 0; i < NumTriangles; i++ )
	{
		uint32 TriangleIndex = StartTriangle + i;
			
		OutCluster.MaterialIndexes[ i ] = InCluster.MaterialIndexes[ TriangleIndex ];
		OutCluster.Indexes[ i * 3 + 0 ] = InCluster.Indexes[ TriangleIndex * 3 + 0 ];
		OutCluster.Indexes[ i * 3 + 1 ] = InCluster.Indexes[ TriangleIndex * 3 + 1 ];
		OutCluster.Indexes[ i * 3 + 2 ] = InCluster.Indexes[ TriangleIndex * 3 + 2 ];
	}

	OutCluster.NumTris = NumTriangles;

	// Rebuild material range and reconstrain 
	OutCluster.BuildMaterialRanges();
#if NANITE_USE_STRIP_INDICES
	ConstrainAndStripifyCluster(OutCluster);
#else
	ConstrainClusterFIFO(OutCluster);
#endif
}

void ConstrainClusters( TArray< FClusterGroup >& ClusterGroups, TArray< FCluster >& Clusters )
{
	// Calculate stats
	uint32 TotalOldTriangles = 0;
	uint32 TotalOldVertices = 0;
	for( const FCluster& Cluster : Clusters )
	{
		TotalOldTriangles += Cluster.NumTris;
		TotalOldVertices += Cluster.Verts.Num();
	}

	ParallelFor(TEXT("NaniteEncode.ConstrainClusters.PF"), Clusters.Num(), 8,
		[&]( uint32 i )
		{
			if( Clusters[i].NumTris )
			{
			#if NANITE_USE_STRIP_INDICES
				ConstrainAndStripifyCluster(Clusters[i]);
			#else
				ConstrainClusterFIFO(Clusters[i]);
			#endif
			}
		} );
	
	uint32 TotalNewTriangles = 0;
	uint32 TotalNewVertices = 0;

	// Constrain clusters
	const uint32 NumOldClusters = Clusters.Num();
	for( uint32 i = 0; i < NumOldClusters; i++ )
	{
		TotalNewTriangles += Clusters[ i ].NumTris;
		TotalNewVertices += Clusters[ i ].Verts.Num();
		
		// Split clusters with too many verts
		if( Clusters[ i ].Verts.Num() > NANITE_MAX_CLUSTER_VERTICES && Clusters[i].NumTris )
		{
			FCluster ClusterA, ClusterB;
			uint32 NumTrianglesA = Clusters[ i ].NumTris / 2;
			uint32 NumTrianglesB = Clusters[ i ].NumTris - NumTrianglesA;
			BuildClusterFromClusterTriangleRange( Clusters[ i ], ClusterA, 0, NumTrianglesA );
			BuildClusterFromClusterTriangleRange( Clusters[ i ], ClusterB, NumTrianglesA, NumTrianglesB );
			Clusters[ i ] = ClusterA;
			// ASSEMBLYTODO Many groups might reference this cluster.
			ClusterGroups[ ClusterB.GroupIndex ].Children.Add( FClusterRef( Clusters.Num() ) );
			Clusters.Add( ClusterB );
		}
	}

	// Calculate stats
	uint32 TotalNewTrianglesWithSplits = 0;
	uint32 TotalNewVerticesWithSplits = 0;
	for( const FCluster& Cluster : Clusters )
	{
		TotalNewTrianglesWithSplits += Cluster.NumTris;
		TotalNewVerticesWithSplits += Cluster.Verts.Num();
	}

	UE_LOG( LogStaticMesh, Log, TEXT("ConstrainClusters:") );
	UE_LOG( LogStaticMesh, Log, TEXT("  Input: %d Clusters, %d Triangles and %d Vertices"), NumOldClusters, TotalOldTriangles, TotalOldVertices );
	UE_LOG( LogStaticMesh, Log, TEXT("  Output without splits: %d Clusters, %d Triangles and %d Vertices"), NumOldClusters, TotalNewTriangles, TotalNewVertices );
	UE_LOG( LogStaticMesh, Log, TEXT("  Output with splits: %d Clusters, %d Triangles and %d Vertices"), Clusters.Num(), TotalNewTrianglesWithSplits, TotalNewVerticesWithSplits );
}

void VerifyClusterConstraints(const FCluster& Cluster)
{
	check(Cluster.NumTris * 3 == Cluster.Indexes.Num());
	check(Cluster.Verts.Num() <= NANITE_MAX_CLUSTER_VERTICES || Cluster.NumTris == 0);

	const uint32 NumTriangles = Cluster.NumTris;

	uint32 MaxVertexIndex = 0;
	for (uint32 i = 0; i < NumTriangles; i++)
	{
		uint32 Index0 = Cluster.Indexes[i * 3 + 0];
		uint32 Index1 = Cluster.Indexes[i * 3 + 1];
		uint32 Index2 = Cluster.Indexes[i * 3 + 2];
		MaxVertexIndex = FMath::Max(MaxVertexIndex, FMath::Max3(Index0, Index1, Index2));
		check(MaxVertexIndex - Index0 < NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE);
		check(MaxVertexIndex - Index1 < NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE);
		check(MaxVertexIndex - Index2 < NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE);
	}
}

void VerifyClusterConstraints( const TArray< FCluster >& Clusters )
{
	ParallelFor(TEXT("NaniteEncode.VerifyClusterConstraints.PF"), Clusters.Num(), 1024,
		[&]( uint32 i )
		{
			VerifyClusterConstraints( Clusters[i] );
		} );
}

} // namespace Nanite
