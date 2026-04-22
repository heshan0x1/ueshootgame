// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClusterDAG.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "GraphPartitioner.h"
#include "NaniteRayTracingScene.h"
#include "BVHCluster.h"
#include "MeshSimplify.h"
#include "Algo/Partition.h"

#define VALIDATE_CLUSTER_ADJACENCY (DO_CHECK && 1)

namespace Nanite
{

void FClusterDAG::AddMesh(
	const FConstMeshBuildVertexView& Verts,
	TArrayView< const uint32 > Indexes,
	TArrayView< const int32 > MaterialIndexes,
	const FBounds3f& VertexBounds,
	const FVertexFormat& VertexFormat )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::ClusterTriangles);

	uint32 Time0 = FPlatformTime::Cycles();

	LOG_CRC( Verts );
	LOG_CRC( Indexes );

	MaxTexCoords		= FMath::Max(MaxTexCoords, VertexFormat.NumTexCoords);
	MaxBoneInfluences	= FMath::Max(MaxBoneInfluences, VertexFormat.NumBoneInfluences);

	bHasSkinning		|= VertexFormat.NumBoneInfluences > 0;
	bHasTangents		|= VertexFormat.bHasTangents;
	bHasColors			|= VertexFormat.bHasColors;

	uint32 NumTriangles = Indexes.Num() / 3;

	FAdjacency Adjacency( Indexes.Num() );
	FEdgeHash EdgeHash( Indexes.Num() );

	auto GetPosition = [ &Verts, &Indexes ]( uint32 EdgeIndex )
	{
		return Verts.Position[ Indexes[ EdgeIndex ] ];
	};

	ParallelFor( TEXT("Nanite.ClusterTriangles.PF"), Indexes.Num(), 4096,
		[&]( int32 EdgeIndex )
		{
			EdgeHash.Add_Concurrent( EdgeIndex, GetPosition );
		} );

	ParallelFor( TEXT("Nanite.ClusterTriangles.PF"), Indexes.Num(), 1024,
		[&]( int32 EdgeIndex )
		{
			int32 AdjIndex = -1;
			int32 AdjCount = 0;
			EdgeHash.ForAllMatching( EdgeIndex, false, GetPosition,
				[&]( int32 EdgeIndex, int32 OtherEdgeIndex )
				{
					AdjIndex = OtherEdgeIndex;
					AdjCount++;
				} );

			if( AdjCount > 1 )
				AdjIndex = -2;

			Adjacency.Direct[ EdgeIndex ] = AdjIndex;
		} );

	FDisjointSet DisjointSet( NumTriangles );

	for( uint32 EdgeIndex = 0, Num = Indexes.Num(); EdgeIndex < Num; EdgeIndex++ )
	{
		if( Adjacency.Direct[ EdgeIndex ] == -2 )
		{
			// EdgeHash is built in parallel, so we need to sort before use to ensure determinism.
			// This path is only executed in the rare event that an edge is shared by more than two triangles,
			// so performance impact should be negligible in practice.
			TArray< TPair< int32, int32 >, TInlineAllocator< 16 > > Edges;
			EdgeHash.ForAllMatching( EdgeIndex, false, GetPosition,
				[&]( int32 EdgeIndex0, int32 EdgeIndex1 )
				{
					Edges.Emplace( EdgeIndex0, EdgeIndex1 );
				} );
			Edges.Sort();	
			
			for( const TPair< int32, int32 >& Edge : Edges )
			{
				Adjacency.Link( Edge.Key, Edge.Value );
			}
		}

		Adjacency.ForAll( EdgeIndex,
			[&]( int32 EdgeIndex0, int32 EdgeIndex1 )
			{
				if( EdgeIndex0 > EdgeIndex1 )
					DisjointSet.UnionSequential( EdgeIndex0 / 3, EdgeIndex1 / 3 );
			} );
	}

	uint32 BoundaryTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log,
		TEXT("Adjacency [%.2fs], tris: %i, UVs %i%s%s"),
		FPlatformTime::ToMilliseconds( BoundaryTime - Time0 ) / 1000.0f,
		Indexes.Num() / 3,
		VertexFormat.NumTexCoords,
		VertexFormat.bHasTangents ? TEXT(", Tangents") : TEXT(""),
		VertexFormat.bHasColors ? TEXT(", Color") : TEXT("") );

#if 0//NANITE_VOXEL_DATA
	FBVHCluster Partitioner( NumTriangles, FCluster::ClusterSize - 4, FCluster::ClusterSize );
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::PartitionGraph);

		Partitioner.Build(
			[ &Verts, &Indexes ]( uint32 TriIndex )
			{
				FBounds3f Bounds;
				Bounds  = Verts.Position[ Indexes[ TriIndex * 3 + 0 ] ];
				Bounds += Verts.Position[ Indexes[ TriIndex * 3 + 1 ] ];
				Bounds += Verts.Position[ Indexes[ TriIndex * 3 + 2 ] ];
				return Bounds;
			} );

		check( Partitioner.Ranges.Num() );

		LOG_CRC( Partitioner.Ranges );
	}
#else
	FGraphPartitioner Partitioner( NumTriangles, FCluster::ClusterSize - 4, FCluster::ClusterSize );

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::PartitionGraph);

		auto GetCenter = [ &Verts, &Indexes ]( uint32 TriIndex )
		{
			FVector3f Center;
			Center  = Verts.Position[ Indexes[ TriIndex * 3 + 0 ] ];
			Center += Verts.Position[ Indexes[ TriIndex * 3 + 1 ] ];
			Center += Verts.Position[ Indexes[ TriIndex * 3 + 2 ] ];
			return Center * (1.0f / 3.0f);
		};
		Partitioner.BuildLocalityLinks( DisjointSet, VertexBounds, MaterialIndexes, GetCenter );

		auto* RESTRICT Graph = Partitioner.NewGraph( NumTriangles * 3 );

		for( uint32 i = 0; i < NumTriangles; i++ )
		{
			Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

			uint32 TriIndex = Partitioner.Indexes[i];

			for( int k = 0; k < 3; k++ )
			{
				Adjacency.ForAll( 3 * TriIndex + k,
					[ &Partitioner, Graph ]( int32 EdgeIndex, int32 AdjIndex )
					{
						Partitioner.AddAdjacency( Graph, AdjIndex / 3, 4 * 65 );
					} );
			}

			Partitioner.AddLocalityLinks( Graph, TriIndex, 1 );
		}
		Graph->AdjacencyOffset[ NumTriangles ] = Graph->Adjacency.Num();

		bool bSingleThreaded = NumTriangles < 5000;

		Partitioner.PartitionStrict( Graph, !bSingleThreaded );
		check( Partitioner.Ranges.Num() );

		LOG_CRC( Partitioner.Ranges );
	}
#endif

	const uint32 OptimalNumClusters = FMath::DivideAndRoundUp< int32 >( Indexes.Num(), FCluster::ClusterSize * 3 );

	uint32 ClusterTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Clustering [%.2fs]. Ratio: %f"), FPlatformTime::ToMilliseconds( ClusterTime - BoundaryTime ) / 1000.0f, (float)Partitioner.Ranges.Num() / (float)OptimalNumClusters );

	const uint32 BaseCluster = Clusters.Num();
	Clusters.AddDefaulted( Partitioner.Ranges.Num() );

	const uint32 MeshIndex = MeshInput.AddDefaulted();
	MeshInput[ MeshIndex ].AddUninitialized( Partitioner.Ranges.Num() );

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::BuildClusters);
		ParallelFor( TEXT("Nanite.BuildClusters.PF"), Partitioner.Ranges.Num(), 1024,
			[&]( int32 Index )
			{
				auto& Range = Partitioner.Ranges[ Index ];

				uint32 ClusterIndex = BaseCluster + Index;

				Clusters[ ClusterIndex ] = FCluster(
					Verts,
					Indexes,
					MaterialIndexes,
					VertexFormat,
					Range.Begin, Range.End,
					Partitioner.Indexes, Partitioner.SortedTo, Adjacency );

				// Negative notes it's a leaf
				Clusters[ ClusterIndex ].EdgeLength *= -1.0f;

				MeshInput[ MeshIndex ][ Index ] = FClusterRef( ClusterIndex );
			});
	}

	for( FCluster& Cluster : Clusters )
		SurfaceArea += Cluster.SurfaceArea;

#if RAY_TRACE_VOXELS
	if( Settings.ShapePreservation == ENaniteShapePreservation::Voxelize )
	{
		for( FCluster& Cluster : Clusters )
			RayTracingScene.AddCluster( Cluster );
	}
#endif

	uint32 LeavesTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Leaves [%.2fs]"), FPlatformTime::ToMilliseconds( LeavesTime - ClusterTime ) / 1000.0f );
}

uint32 FClusterDAG::FindAdjacentClusters( 
	TArray< TMap< uint32, uint32 > >& OutAdjacency,
	TArrayView< const FClusterRef > LevelClusters, 
	uint32 NumExternalEdges )
{
	struct FExternalEdge
	{
		uint32	ClusterRefIndex;
		int32	EdgeIndex;
	};
	TArray< FExternalEdge >	ExternalEdges;
	FHashTable				ExternalEdgeHash;
	TAtomic< uint32 >		ExternalEdgeOffset(0);

	OutAdjacency.AddDefaulted( LevelClusters.Num() );

	// We have a total count of NumExternalEdges so we can allocate a hash table without growing.
	ExternalEdges.AddUninitialized( NumExternalEdges );
	ExternalEdgeHash.Clear( 1 << FMath::FloorLog2( NumExternalEdges ), NumExternalEdges );

	// Add edges to hash table
	ParallelFor( TEXT("Nanite.EdgeHashAdd.PF"), LevelClusters.Num(), 32,
		[&]( uint32 ClusterRefIndex )
		{
			FCluster& Cluster = LevelClusters[ ClusterRefIndex ].GetCluster( *this );

			for( int32 EdgeIndex = 0; EdgeIndex < Cluster.ExternalEdges.Num(); EdgeIndex++ )
			{
				if( Cluster.ExternalEdges[ EdgeIndex ] )
				{
					uint32 VertIndex0 = Cluster.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Cluster.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector3f& Position0 = Cluster.Verts.GetPosition( VertIndex0 );
					const FVector3f& Position1 = Cluster.Verts.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash0, Hash1 } );

					uint32 ExternalEdgeIndex = ExternalEdgeOffset++;
					ExternalEdges[ ExternalEdgeIndex ] = { ClusterRefIndex, EdgeIndex };
					ExternalEdgeHash.Add_Concurrent( Hash, ExternalEdgeIndex );
				}
			}
		} );

	check( ExternalEdgeOffset == ExternalEdges.Num() );

	std::atomic< uint32 > NumAdjacency(0);

	// Find matching edge in other clusters
	ParallelFor( TEXT("Nanite.FindMatchingEdge.PF"), LevelClusters.Num(), 32,
		[&]( uint32 ClusterRefIndex )
		{
			FCluster& Cluster = LevelClusters[ ClusterRefIndex ].GetCluster( *this );
			TMap< uint32, uint32 >& AdjacentClusters = OutAdjacency[ ClusterRefIndex ];

			for( int32 EdgeIndex = 0; EdgeIndex < Cluster.ExternalEdges.Num(); EdgeIndex++ )
			{
				if( Cluster.ExternalEdges[ EdgeIndex ] )
				{
					uint32 VertIndex0 = Cluster.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Cluster.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector3f& Position0 = Cluster.Verts.GetPosition( VertIndex0 );
					const FVector3f& Position1 = Cluster.Verts.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash1, Hash0 } );

					for( uint32 ExternalEdgeIndex = ExternalEdgeHash.First( Hash ); ExternalEdgeHash.IsValid( ExternalEdgeIndex ); ExternalEdgeIndex = ExternalEdgeHash.Next( ExternalEdgeIndex ) )
					{
						FExternalEdge ExternalEdge = ExternalEdges[ ExternalEdgeIndex ];

						FCluster& OtherCluster = LevelClusters[ ExternalEdge.ClusterRefIndex ].GetCluster( *this );

						if( OtherCluster.ExternalEdges[ ExternalEdge.EdgeIndex ] )
						{
							uint32 OtherVertIndex0 = OtherCluster.Indexes[ ExternalEdge.EdgeIndex ];
							uint32 OtherVertIndex1 = OtherCluster.Indexes[ Cycle3( ExternalEdge.EdgeIndex ) ];
			
							if( Position0 == OtherCluster.Verts.GetPosition( OtherVertIndex1 ) &&
								Position1 == OtherCluster.Verts.GetPosition( OtherVertIndex0 ) )
							{
								if( ClusterRefIndex != ExternalEdge.ClusterRefIndex )
								{
									// Increase its count
									AdjacentClusters.FindOrAdd( ExternalEdge.ClusterRefIndex, 0 )++;

									// Can't break or a triple edge might be non-deterministically connected.
									// Need to find all matching, not just first.
								}
							}
						}
					}
				}
			}
			NumAdjacency += AdjacentClusters.Num();

			// Force deterministic order of adjacency.
			AdjacentClusters.KeySort(
				[&]( uint32 A, uint32 B )
				{
					return LevelClusters[A].GetCluster( *this ).GUID < LevelClusters[B].GetCluster( *this ).GUID;
				} );
		} );

#if VALIDATE_CLUSTER_ADJACENCY
	// Validate the bi-directionality of adjacency. Also, since only roots are currently instanced, validate that
	// no instanced clusters have adjacency.
	for( int32 i = 0; i < LevelClusters.Num(); ++i )
	{
		check( OutAdjacency[ i ].Num() == 0 || !LevelClusters[ i ].IsInstance() );
		for( const auto& KeyValue : OutAdjacency[ i ] )
		{
			check( KeyValue.Value > 0 );
			check( KeyValue.Value == OutAdjacency[ KeyValue.Key ].FindChecked( i ));
			check( !LevelClusters[ KeyValue.Key ].IsInstance() );			
		}
	}
#endif

	return NumAdjacency;
}

static const uint32 MinGroupSize = 8;
static const uint32 MaxGroupSize = 32;

uint32 GetMaxParents( const FClusterGroup& Group, FClusterDAG& DAG, uint32 MaxClusterSize )
{
	uint32 NumGroupElements = 0;
	for( FClusterRef Child : Group.Children )
	{
		NumGroupElements += Child.GetCluster( DAG ).MaterialIndexes.Num();
	}
	return FMath::DivideAndRoundUp( NumGroupElements, MaxClusterSize * 2 );
}

void FClusterDAG::GroupTriangleClusters( TArrayView< const FClusterRef > LevelClusters, uint32 NumExternalEdges )
{
	if( LevelClusters.IsEmpty() )
		return;

	if( LevelClusters.Num() <= MaxGroupSize )
	{
		FClusterGroup& Group = Groups.AddDefaulted_GetRef();
		Group.Children.Append( LevelClusters );
		return;
	}

	TArray< TMap< uint32, uint32 > > Adjacency;
	const uint32 NumAdjacency = FindAdjacentClusters( Adjacency, LevelClusters, NumExternalEdges );

	FDisjointSet DisjointSet( LevelClusters.Num() );

	for( uint32 ClusterRefIndex = 0; ClusterRefIndex < (uint32)LevelClusters.Num(); ClusterRefIndex++ )
	{
		for( const auto& Pair : Adjacency[ ClusterRefIndex ] )
		{
			const uint32 OtherClusterRefIndex = Pair.Key;
			if( ClusterRefIndex > OtherClusterRefIndex )
			{
				DisjointSet.UnionSequential( ClusterRefIndex, OtherClusterRefIndex );
			}
		}
	}

	FGraphPartitioner Partitioner( LevelClusters.Num(), MinGroupSize, MaxGroupSize );

	// TODO Cache this
	auto GetCenter = [&]( uint32 ClusterRefIndex )
	{
		FClusterRef ClusterRef = LevelClusters[ ClusterRefIndex ];
		FBounds3f& Bounds = ClusterRef.GetCluster( *this ).Bounds;
		FVector3f Center = 0.5f * ( Bounds.Min + Bounds.Max );

		if( ClusterRef.IsInstance() )
			Center = ClusterRef.GetTransform( *this ).TransformPosition( Center );
			
		return Center;
	};
	Partitioner.BuildLocalityLinks( DisjointSet, TotalBounds, TArrayView< const int32 >(), GetCenter );

	auto* RESTRICT Graph = Partitioner.NewGraph( NumAdjacency );

	for( int32 i = 0; i < LevelClusters.Num(); i++ )
	{
		Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

		uint32 ClusterRefIndex = Partitioner.Indexes[i];

		const FCluster& Cluster = LevelClusters[ ClusterRefIndex ].GetCluster( *this );
		for( const auto& Pair : Adjacency[ ClusterRefIndex ] )
		{
			uint32 OtherClusterRefIndex = Pair.Key;
			uint32 NumSharedEdges = Pair.Value;

			const FCluster& OtherCluster = LevelClusters[ OtherClusterRefIndex ].GetCluster( *this );

			bool bSiblings = Cluster.GeneratingGroupIndex != MAX_uint32 && Cluster.GeneratingGroupIndex == OtherCluster.GeneratingGroupIndex;

			Partitioner.AddAdjacency( Graph, OtherClusterRefIndex, NumSharedEdges * ( bSiblings ? 12 : 16 ) + 4 );
		}

		Partitioner.AddLocalityLinks( Graph, ClusterRefIndex, 1 );
	}
	Graph->AdjacencyOffset[ Graph->Num ] = Graph->Adjacency.Num();

	LOG_CRC( Graph->Adjacency );
	LOG_CRC( Graph->AdjacencyCost );
	LOG_CRC( Graph->AdjacencyOffset );
		
	bool bSingleThreaded = LevelClusters.Num() <= 32;

	Partitioner.PartitionStrict( Graph, !bSingleThreaded );

	LOG_CRC( Partitioner.Ranges );

	for( auto& Range : Partitioner.Ranges )
	{
		FClusterGroup& Group = Groups.AddDefaulted_GetRef();

		for( uint32 i = Range.Begin; i < Range.End; i++ )
			Group.Children.Add( LevelClusters[ Partitioner.Indexes[i] ] );
	}
}

void FClusterDAG::GroupVoxelClusters( TArrayView< const FClusterRef > LevelClusters )
{
	if( LevelClusters.IsEmpty() )
		return;

	// TODO If Clusters were compacted instead of resorted this wouldn't be needed.
	LevelClusters.Sort(
		[&]( FClusterRef A, FClusterRef B )
		{
			if( !A.IsInstance() || !B.IsInstance() || A.InstanceIndex == B.InstanceIndex )
				return A.GetCluster( *this ).GeneratingGroupIndex < B.GetCluster( *this ).GeneratingGroupIndex;
			else
				return A.InstanceIndex < B.InstanceIndex;
		} );

	int32 GroupOffset = Groups.Num();

	{
		uint32 RunInstanceIndex = ~0u;
		uint32 RunGroupIndex = ~0u;
		for( FClusterRef ClusterRef : LevelClusters )
		{
			uint32 InstanceIndex = ClusterRef.IsInstance() ? ClusterRef.InstanceIndex : ~0u;
			uint32 GroupIndex = ClusterRef.GetCluster( *this ).GeneratingGroupIndex;
			if( RunInstanceIndex	!= InstanceIndex ||
				RunGroupIndex		!= GroupIndex )
			{
				RunInstanceIndex = InstanceIndex;
				RunGroupIndex = GroupIndex;

				FClusterGroup& Group = Groups.AddDefaulted_GetRef();
				FClusterGroup& GeneratingGroup = Groups[ GroupIndex ];

				Group.Bounds			= GeneratingGroup.Bounds;
				Group.ParentLODError	= GeneratingGroup.ParentLODError;

				if( ClusterRef.IsInstance() )
				{
					const FMatrix44f& Transform = ClusterRef.GetTransform( *this );
					Group.Bounds.Center = Transform.TransformPosition( Group.Bounds.Center );

					const float MaxScale = Transform.GetScaleVector().GetMax();
					Group.Bounds.W *= MaxScale;
					Group.ParentLODError *= MaxScale;
				}
			}
			
			Groups.Last().Children.Add( ClusterRef );

			check( Groups.Last().Children.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP_TARGET );
		}
	}

	TArrayView< FClusterGroup > LevelGroups( Groups.GetData() + GroupOffset, Groups.Num() - GroupOffset );

	TArray< uint32 > SortKeys;
	TArray< int32 > Input, Output;
	SortKeys.AddUninitialized( LevelGroups.Num() );
	Input.AddUninitialized( LevelGroups.Num() );
	Output.AddUninitialized( LevelGroups.Num() );

	ParallelFor( TEXT("GroupVoxelClusters.SortKeys.PF"), LevelGroups.Num(), 4096,
		[&]( int32 Index )
		{
			FVector3f Center = LevelGroups[ Index ].Bounds.Center;
			FVector3f CenterLocal = ( Center - TotalBounds.Min ) / FVector3f( TotalBounds.Max - TotalBounds.Min ).GetMax();

			uint32 Morton;
			Morton  = FMath::MortonCode3( uint32( CenterLocal.X * 1023 ) );
			Morton |= FMath::MortonCode3( uint32( CenterLocal.Y * 1023 ) ) << 1;
			Morton |= FMath::MortonCode3( uint32( CenterLocal.Z * 1023 ) ) << 2;
			SortKeys[ Index ] = Morton;
			Input[ Index ] = Index;
		} );

	RadixSort32( Output.GetData(), Input.GetData(), Input.Num(),
		[&]( int32 Index )
		{
			return SortKeys[ Index ];
		} );

	TArrayView< int32 > MergeIndex( (int32*)SortKeys.GetData(), SortKeys.Num() );

	while( Output.Num() > 1 )
	{
		Swap( Input, Output );
		Output.Reset();

		const int32 SearchRadius = 16;
		uint32 PossibleMerges = 0;

		// Find least cost neighbor
		ParallelFor( TEXT("GroupVoxelClusters.LeastCostIndex.PF"), Input.Num(), 4096,
			[&]( int32 i )
			{
				const FClusterGroup& Group0 = LevelGroups[ Input[i] ];

				float LeastCost = MAX_flt;
				int32 LeastCostIndex = -1;

				int32 SearchMin = FMath::Max( i - SearchRadius, 0 );
				int32 SearchMax = FMath::Min( i + SearchRadius + 1, Input.Num() );
				for( int32 NeighborIndex = SearchMin; NeighborIndex < SearchMax; NeighborIndex++ )
				{
					if( NeighborIndex == i )
						continue;

					const FClusterGroup& Group1 = LevelGroups[ Input[ NeighborIndex ] ];

					bool bTooSmall = 
						Group0.Children.Num() < MinGroupSize ||
						Group1.Children.Num() < MinGroupSize;
					bool bTooLarge = Group0.Children.Num() + Group1.Children.Num() > MaxGroupSize;

					if( bTooSmall && !bTooLarge )
					{
						float Cost = ( Group0.Bounds + Group1.Bounds ).W;

						// TODO include difference in error
						if( Cost < LeastCost )
						{
							LeastCost = Cost;
							LeastCostIndex = NeighborIndex;
							PossibleMerges = 1;
						}
					}
				}

				MergeIndex[i] = LeastCostIndex;
			} );

		if( PossibleMerges == 0 )
		{
			Swap( Input, Output );
			break;
		}

		// Merge pass
		for( int32 i = 0; i < Input.Num(); i++ )
		{
			int32 Merge1 = MergeIndex[i];
			int32 Merge0 = Merge1 >= 0 ? MergeIndex[ Merge1 ] : -1;
			if( i == Merge0 )
			{
				// Matching pair, merge
				if( i < Merge1 )
				{
					// Left side: Merge
					FClusterGroup& Group0 = LevelGroups[ Input[i] ];
					FClusterGroup& Group1 = LevelGroups[ Input[ Merge1 ] ];

					Group0.Children.Append( Group1.Children );
					Group1.Children.Empty();

					Group0.Bounds += Group1.Bounds;

					check( Group0.Children.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP_TARGET );

					Output.Add( Input[i] );
				}
				else
				{
					// Right side: Do nothing because left side owns merging
				}
			}
			else
			{
				// Not valid to merge this pass. Just copy for next time.
				Output.Add( Input[i] );
			}
		}
	}

	// Remove empty groups
	for( int32 i = GroupOffset; i < Groups.Num(); )
	{
		if( Groups[i].Children.IsEmpty() )
			Groups.RemoveAtSwap( i, EAllowShrinking::No );
		else
			i++;
	}
}

void FClusterDAG::ReduceMesh( uint32 MeshIndex )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::DAG.ReduceMesh);

	if( MeshInput[ MeshIndex ].IsEmpty() )
		return;

#if RAY_TRACE_VOXELS
	if( Settings.ShapePreservation == ENaniteShapePreservation::Voxelize )
		rtcCommitScene( RayTracingScene.Scene );
#endif

	std::atomic< int32 > NumClusters( Clusters.Num() );

	UE::Tasks::FCancellationToken* CancellationToken = UE::Tasks::FCancellationTokenScope::GetCurrentCancellationToken();
	while( true )
	{
		if (CancellationToken && CancellationToken->IsCanceled())
		{
			return;
		}

		TArrayView< FClusterRef > LevelClusters = MeshInput[ MeshIndex ];

		int32 ClusterOffset = Clusters.Num();
		int32 GroupOffset = Groups.Num();

		uint32 NumExternalEdges = 0;

		float MinError = +MAX_flt;
		float MaxError = -MAX_flt;
		float AvgError = 0.0f;

		for( FClusterRef ClusterRef : LevelClusters )
		{
			const FCluster& Cluster = ClusterRef.GetCluster( *this );
			
			FBounds3f Bounds = Cluster.Bounds;
			float	LODError = Cluster.LODError;

			if( ClusterRef.IsInstance() )
			{
				const FMatrix44f& Transform = ClusterRef.GetTransform( *this );
				Bounds = Bounds.TransformBy( Transform );
				LODError *= Transform.GetScaleVector().GetMax();
			}

			NumExternalEdges += Cluster.NumExternalEdges;
			TotalBounds += Bounds;

			MinError = FMath::Min( MinError, LODError );
			MaxError = FMath::Max( MaxError, LODError );
			AvgError += LODError;
		}
		AvgError /= (float)LevelClusters.Num();

		UE_LOG( LogStaticMesh, Verbose, TEXT("Num clusters %i. Error %.4f, %.4f, %.4f"), LevelClusters.Num(), MinError, AvgError, MaxError );

		uint32 MaxClusterSize = FCluster::ClusterSize;
		if( LevelClusters.Num() == 1 )
		{
			const FCluster& Cluster = LevelClusters[0].GetCluster( *this );

			if( Cluster.NumTris )
				break;
			else if( Cluster.MaterialIndexes.Num() > 64 && Settings.ExtraVoxelLevels >= 1 )
				MaxClusterSize = 64;
			else if( Cluster.MaterialIndexes.Num() > 32 && Settings.ExtraVoxelLevels >= 2 )
				MaxClusterSize = 32;
			else
				break;
		}

		ON_SCOPE_EXIT
		{
			if (CancellationToken && CancellationToken->IsCanceled())
			{
				return;
			}

			check( ClusterOffset < NumClusters );

			// Correct num to atomic count
			Clusters.SetNum( NumClusters, EAllowShrinking::No );
		
			MeshInput[ MeshIndex ].Reset();
			for( uint32 ClusterIndex = ClusterOffset; ClusterIndex < (uint32)Clusters.Num(); ClusterIndex++ )
				MeshInput[ MeshIndex ].Add( FClusterRef( ClusterIndex ) );
		};
		
		if( LevelClusters.Num() <= MaxGroupSize )
		{
			int32 GroupIndex = Groups.AddDefaulted();

			Groups[ GroupIndex ].Children.Append( LevelClusters );

			uint32 MaxParents = GetMaxParents( Groups[ GroupIndex ], *this, MaxClusterSize );
			Clusters.AddDefaulted( MaxParents );

			ReduceGroup( NumClusters, MaxClusterSize, MaxParents, GroupIndex, MeshIndex );

			continue;
		}

		int32 NumTriClusters = Algo::Partition( LevelClusters,
			[ this ]( FClusterRef ClusterRef )
			{
				return ClusterRef.GetCluster( *this ).NumTris > 0;
			} );

		TArrayView< FClusterRef > TriClusters	= LevelClusters.Slice( 0, NumTriClusters );
		TArrayView< FClusterRef > VoxelClusters	= LevelClusters.Slice( NumTriClusters, LevelClusters.Num() - NumTriClusters );

		if( TriClusters.Num() == 1 )
		{
			TriClusters = TArrayView< FClusterRef >();
			VoxelClusters = LevelClusters;
		}

		if( VoxelClusters.Num() == 1 )
		{
			VoxelClusters = TArrayView< FClusterRef >();
			TriClusters = LevelClusters;
		}
		
		GroupTriangleClusters( TriClusters, NumExternalEdges );
		GroupVoxelClusters( VoxelClusters );

		TArrayView< FClusterGroup > LevelGroups( Groups.GetData() + GroupOffset, Groups.Num() - GroupOffset );

		uint32 MaxParents = 0;
		for( const FClusterGroup& Group : LevelGroups )
			MaxParents += GetMaxParents( Group, *this, MaxClusterSize );

		Clusters.AddDefaulted( MaxParents );

		ParallelFor( TEXT("Nanite.ReduceGroup.PF"), LevelGroups.Num(), 1,
			[&]( int32 i )
			{
				if (CancellationToken && CancellationToken->IsCanceled())
				{
					return;
				}

				uint32 MaxParents = GetMaxParents( LevelGroups[i], *this, MaxClusterSize );

				ReduceGroup( NumClusters, MaxClusterSize, MaxParents, GroupOffset + i, MeshIndex );
			},
			EParallelForFlags::Unbalanced );

		if (CancellationToken && CancellationToken->IsCanceled())
		{
			return;
		}
		// Force a deterministic order of the generated parent clusters
		{
			// TODO: Optimize me.
			// Just sorting the array directly seems like the safest option at this stage (right before UE5 final build).
			// On AOD_Shield this seems to be on the order of 0.01s in practice.
			// As the Clusters array is already conservatively allocated, it seems storing the parent clusters in their designated
			// conservative ranges and then doing a compaction pass at the end would be a more efficient solution that doesn't involve sorting.
			
			//uint32 StartTime = FPlatformTime::Cycles();
			TArrayView< FCluster > Parents( &Clusters[ ClusterOffset ], NumClusters - ClusterOffset );
			Parents.Sort(
				[&]( const FCluster& A, const FCluster& B )
				{
					return A.GUID < B.GUID;
				} );
			//UE_LOG(LogStaticMesh, Log, TEXT("SortTime Adjacency [%.2fs]"), FPlatformTime::ToMilliseconds(FPlatformTime::Cycles() - StartTime) / 1000.0f);
		}
	}

#if RAY_TRACE_VOXELS
	// Clear ExtraVoxels for all clusters except the root as it might be needed for assembly composition.
	for( int32 i = 0; i < int32( Clusters.Num() ) - 1; i++ )
	{
		Clusters[ i ].ExtraVoxels.Empty();
	}
#endif
	
	// Max out root node
	const FClusterRef& LastClusterRef	= MeshInput[MeshIndex].Last();
	FCluster& LastCluster				= LastClusterRef.GetCluster(*this);
	FSphere3f LODBounds 				= LastCluster.LODBounds;

	if( LastClusterRef.IsInstance() )
	{
		// Corner case: The root cluster comes from a single assembly part
		const FMatrix44f& Transform = LastClusterRef.GetTransform( *this );
		LODBounds.Center = Transform.TransformPosition( LODBounds.Center );
		
		const float MaxScale = Transform.GetScaleVector().GetMax();
		LODBounds.W *= MaxScale;

		AssemblyInstanceData[ LastClusterRef.InstanceIndex ].LODBounds		= LODBounds;
		AssemblyInstanceData[ LastClusterRef.InstanceIndex ].ParentLODError	= 1e10f;
	}
	
	FClusterGroup RootClusterGroup;
	RootClusterGroup.Children.Add( LastClusterRef );
	RootClusterGroup.Bounds				= LastCluster.SphereBounds;
	RootClusterGroup.LODBounds			= LODBounds;
	RootClusterGroup.ParentLODError		= 1e10f;
	RootClusterGroup.MipLevel			= LastCluster.MipLevel;
	RootClusterGroup.MeshIndex			= MeshIndex;
	RootClusterGroup.bRoot				= true;
	LastCluster.GroupIndex 				= Groups.Num();
	Groups.Add(RootClusterGroup);

	// Clear the root cluster's external edges
	FMemory::Memzero(LastCluster.ExternalEdges.GetData(), LastCluster.ExternalEdges.Num());
	LastCluster.NumExternalEdges = 0;
}

template< typename FPartitioner, typename FPartitionFunc >
bool SplitCluster( FCluster& Merged, TArray< FCluster >& Clusters, std::atomic< int32 >& NumClusters, uint32 MaxClusterSize, uint32& NumParents, uint32& ParentStart, uint32& ParentEnd, FPartitionFunc&& PartitionFunc )
{
	if( Merged.MaterialIndexes.Num() <= (int32)MaxClusterSize )
	{
		ParentEnd = ( NumClusters += 1 );
		ParentStart = ParentEnd - 1;

		Clusters[ ParentStart ] = Merged;
		Clusters[ ParentStart ].Bound();
		return true;
	}
	else if( NumParents > 1 )
	{
		check( MaxClusterSize == FCluster::ClusterSize );

		FAdjacency Adjacency = Merged.BuildAdjacency();
		FPartitioner Partitioner( Merged.MaterialIndexes.Num(), MaxClusterSize - 4, MaxClusterSize );
		PartitionFunc( Partitioner, Adjacency );

		if( Partitioner.Ranges.Num() <= (int32)NumParents )
		{
			NumParents = Partitioner.Ranges.Num();
			ParentEnd = ( NumClusters += NumParents );
			ParentStart = ParentEnd - NumParents;

			int32 Parent = ParentStart;
			for( auto& Range : Partitioner.Ranges )
			{
				Clusters[ Parent ] = FCluster( Merged, Range.Begin, Range.End, Partitioner.Indexes, Partitioner.SortedTo, Adjacency );
				Parent++;
			}

			return true;
		}
	}

	return false;
}

void FClusterDAG::ReduceGroup( std::atomic< int32 >& NumClusters, uint32 MaxClusterSize, uint32 NumParents, int32 GroupIndex, uint32 MeshIndex )
{
	FClusterGroup& Group = Groups[ GroupIndex ];

	check( Group.Children.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP_TARGET );

	bool bAnyTriangles = false;
	bool bAllTriangles = true;

	uint32 GroupNumVerts = 0;
	float  GroupArea = 0.0f;

	TArray< FSphere3f, TInlineAllocator< MaxGroupSize > > Children_Bounds;
	TArray< FSphere3f, TInlineAllocator< MaxGroupSize > > Children_LODBounds;

	for( FClusterRef Child : Group.Children )
	{
		FCluster& Cluster = Child.GetCluster( *this );

		GroupNumVerts += Cluster.Verts.Num();

		bAnyTriangles = bAnyTriangles || Cluster.NumTris > 0;
		bAllTriangles = bAllTriangles && Cluster.NumTris > 0;

		bool bLeaf = Cluster.EdgeLength < 0.0f;

		FSphere3f	SphereBounds	= Cluster.SphereBounds;
		FSphere3f	LODBounds		= Cluster.LODBounds;
		float		LODError		= Cluster.LODError;

		if( Child.IsInstance() )
		{
			const FMatrix44f& Transform = Child.GetTransform( *this );

			SphereBounds.Center	= Transform.TransformPosition( SphereBounds.Center );
			LODBounds.Center	= Transform.TransformPosition( LODBounds.Center );

			const float MaxScale = Transform.GetScaleVector().GetMax();
			SphereBounds.W	*= MaxScale;
			LODBounds.W		*= MaxScale;
			LODError		*= MaxScale;

			GroupArea += Cluster.SurfaceArea * FMath::Square( MaxScale );
		}
		else
		{
			// Instanced children are already owned by a group.
			Cluster.GroupIndex = GroupIndex;

			GroupArea += Cluster.SurfaceArea;
		}

		// Force monotonic nesting.
		Children_Bounds.Add( SphereBounds );
		Children_LODBounds.Add( LODBounds );
		Group.ParentLODError	= FMath::Max( Group.ParentLODError, LODError );
		Group.MipLevel			= FMath::Max( Group.MipLevel, Cluster.MipLevel );
	}

	Group.Bounds	= FSphere3f( Children_Bounds.GetData(),		Children_Bounds.Num() );
	Group.LODBounds	= FSphere3f( Children_LODBounds.GetData(),	Children_LODBounds.Num() );
	Group.MeshIndex	= MeshIndex;

	uint32 ParentStart = 0;
	uint32 ParentEnd = 0;

	FCluster Merged;
	float SimplifyError = MAX_flt;

	bool bVoxels = false;
	bVoxels = !bAllTriangles || Settings.ShapePreservation == ENaniteShapePreservation::Voxelize;

	uint32 TargetClusterSize = MaxClusterSize - 2;
	if( bAllTriangles )
	{
		uint32 TargetNumTris = NumParents * TargetClusterSize;

		if( !bVoxels ||
			Settings.VoxelLevel == 0 ||
			Settings.VoxelLevel > Group.MipLevel + 1 )
		{
			Merged = FCluster( *this, Group.Children );
			SimplifyError = Merged.Simplify( *this, TargetNumTris );
		}
	}

	if( bVoxels )
	{
		int32 TargetNumBricks = NumParents * MaxClusterSize;
		//uint32 TargetNumVoxels = TargetNumBricks * 16;
		uint32 TargetNumVoxels = FMath::Max( 1u, ( GroupNumVerts * 3 ) / 4 );

		float VoxelSize = FMath::Sqrt( GroupArea / float(TargetNumVoxels) );
		VoxelSize *= 0.75f;

		VoxelSize = FMath::Max( VoxelSize, Group.ParentLODError );

	#if 0
		// Round to pow2
		// = exp2( floor( log2(x) + 0.5 ) )
		FFloat32 VoxelSizeF( VoxelSize * UE_SQRT_2 );
		VoxelSizeF.Components.Mantissa = 0;
		VoxelSize = VoxelSizeF.FloatValue;
	#endif

		check( VoxelSize > 0.0f );
		check( FMath::IsFinite( VoxelSize ) );

		float EstimatedVoxelSize = VoxelSize;

		while( VoxelSize < SimplifyError )
		{
			FCluster Voxelized;
			Voxelized.Voxelize( *this, Group.Children, VoxelSize );

			if( Voxelized.Verts.Num()	<= TargetNumVoxels &&
				Voxelized.Bricks.Num()	<= TargetNumBricks )
			{
				bool bSplitSuccess = SplitCluster< FBVHCluster >( Voxelized, Clusters, NumClusters, MaxClusterSize, NumParents, ParentStart, ParentEnd,
					[ &Voxelized ]( FBVHCluster& Partitioner, FAdjacency& Adjacency )
					{
						Partitioner.Build(
							[ &Voxelized ]( uint32 VertIndex )
							{
								FBounds3f Bounds;
								Bounds = FVector3f( Voxelized.Bricks[ VertIndex ].Position );
								return Bounds;
							} );
					} );

				// Voxel clusters will never be split up so pass on all extra data to first parent
			#if RAY_TRACE_VOXELS
				Clusters[ ParentStart ].ExtraVoxels.Append( Voxelized.ExtraVoxels );
			#endif

				check( bSplitSuccess );
				break;
			}

			VoxelSize *= 1.1f;
			check( FMath::IsFinite( VoxelSize ) );
		}

		if( VoxelSize < SimplifyError )
			SimplifyError = VoxelSize;
		else
			bVoxels = false;
	}

	if( !bVoxels )
	{
		check( bAllTriangles );

		while(1)
		{
			bool bSplitSuccess = SplitCluster< FGraphPartitioner >( Merged, Clusters, NumClusters, MaxClusterSize, NumParents, ParentStart, ParentEnd,
				[ &Merged ]( FGraphPartitioner& Partitioner, FAdjacency& Adjacency )
				{
					Merged.Split( Partitioner, Adjacency );
				} );

			if( bSplitSuccess )
				break;

			TargetClusterSize -= 2;
			if( TargetClusterSize <= MaxClusterSize / 2 )
				break;

			uint32 TargetNumTris = NumParents * TargetClusterSize;

			// Start over from scratch. Continuing from simplified cluster screws up ExternalEdges and LODError.
			Merged = FCluster( *this, Group.Children );
			SimplifyError = Merged.Simplify( *this, TargetNumTris );
		}
	}

	Group.ParentLODError = FMath::Max( Group.ParentLODError, SimplifyError );

	// Force parents to have same LOD data. They are all dependent.
	for( uint32 Parent = ParentStart; Parent < ParentEnd; Parent++ )
	{
		Clusters[ Parent ].LODBounds			= Group.LODBounds;
		Clusters[ Parent ].LODError				= Group.ParentLODError;
		Clusters[ Parent ].GeneratingGroupIndex = GroupIndex;
	}

	for( FClusterRef Child : Group.Children )
	{
		if( Child.IsInstance() )
		{
			AssemblyInstanceData[ Child.InstanceIndex ].LODBounds		= Group.LODBounds;
			AssemblyInstanceData[ Child.InstanceIndex ].ParentLODError	= Group.ParentLODError;
		}
	}
}

void FClusterDAG::FindCut(
	TArray< FClusterRef >&	SelectedClusters,
	TSet< uint32 >&			SelectedGroups,
	uint32 TargetNumTris,
	float  TargetError,
	uint32 TargetOvershoot ) const
{
	// TODO After traversing into a part, how to know which transform?
	// Need absolute transform stack just like visible clusters.
	// TODO Scale error.

	TSet< uint64 > SelectedGroups2;

	bool bHitTargetBefore = false;
	uint32 NumTris = 0;
	float MinError = 0.0f;

	const FClusterGroup& RootGroup = Groups.Last();
	for( FClusterRef Child : RootGroup.Children )
	{
		SelectedClusters.Add(Child);

		const FCluster& RootCluster = Child.GetCluster( *this );

		float RootLODError = RootCluster.LODError;

		if (Child.IsInstance())
		{
			const FMatrix44f& Transform = Child.GetTransform(*this);
			RootLODError *= Transform.GetScaleVector().GetMax();
		}

		NumTris += RootCluster.NumTris;
		MinError = RootLODError;
	}
	SelectedGroups.Add( Groups.Num() - 1 );
	SelectedGroups2.Add( Groups.Num() - 1 );

	auto LargestError = [ this ]( FClusterRef A, FClusterRef B )
	{
		const FCluster& ClusterA = A.GetCluster( *this );
		const FCluster& ClusterB = B.GetCluster( *this );

		float LODErrorA = ClusterA.LODError;

		if (A.IsInstance())
		{
			const FMatrix44f& Transform = A.GetTransform(*this);
			LODErrorA *= Transform.GetScaleVector().GetMax();
		}

		float LODErrorB = ClusterB.LODError;

		if (B.IsInstance())
		{
			const FMatrix44f& Transform = B.GetTransform(*this);
			LODErrorB *= Transform.GetScaleVector().GetMax();
		}

		return LODErrorA > LODErrorB;
	};

	while( true )
	{
		// Grab highest error cluster to replace to reduce cut error
		const FClusterRef ClusterRef = SelectedClusters.HeapTop();
		const FCluster& Cluster = ClusterRef.GetCluster( *this );

		if( Cluster.MipLevel == 0 )
			break;
		if( Cluster.GeneratingGroupIndex == MAX_uint32 )
			break;

		bool bHitTarget = NumTris > TargetNumTris || MinError < TargetError;

		// Overshoot the target by TargetOvershoot number of triangles. This allows granular edge collapses to better minimize error to the target.
		if( TargetOvershoot > 0 && bHitTarget && !bHitTargetBefore )
		{
			TargetNumTris = NumTris + TargetOvershoot;
			bHitTarget = false;
			bHitTargetBefore = true;
		}

		float LODError = Cluster.LODError;

		if (ClusterRef.IsInstance())
		{
			const FMatrix44f& Transform = ClusterRef.GetTransform(*this);
			LODError *= Transform.GetScaleVector().GetMax();
		}

		if( bHitTarget && LODError < MinError )
			break;
		
		SelectedClusters.HeapPopDiscard( LargestError, EAllowShrinking::No );
		NumTris -= Cluster.NumTris;

		check(LODError <= MinError );
		MinError = LODError;

		bool bAlreadyAdded = false;
		SelectedGroups.FindOrAdd( Cluster.GeneratingGroupIndex, &bAlreadyAdded );
		SelectedGroups2.FindOrAdd( uint64(Cluster.GeneratingGroupIndex) << 32 | ClusterRef.InstanceIndex, &bAlreadyAdded );
		// There will be other parent clusters with the same LODError from the same GeneratingGroupIndex still on the heap.
		if( !bAlreadyAdded )
		{
			for( FClusterRef Child : Groups[ Cluster.GeneratingGroupIndex ].Children )
			{
				const FCluster& ChildCluster = Child.GetCluster( *this );

				float ChildLODError = ChildCluster.LODError;

				if (!Child.IsInstance())
				{
					// hack FClusterRef to point to parent InstanceIndex
					Child = FClusterRef( ClusterRef.InstanceIndex, Child.ClusterIndex );
				}

				if (Child.IsInstance())
				{
					const FMatrix44f& Transform = Child.GetTransform(*this);
					ChildLODError *= Transform.GetScaleVector().GetMax();
				}

				check( ChildCluster.MipLevel < Cluster.MipLevel );
				check( ChildLODError <= MinError );
				SelectedClusters.HeapPush( Child, LargestError );
				NumTris += ChildCluster.NumTris;
			}
		}
	}
}

} // namespace Nanite
