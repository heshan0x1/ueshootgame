// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster.h"
#include "Math/Bounds.h"
#include "NaniteRayTracingScene.h"
#include "Rendering/NaniteResources.h"

// Log CRCs to test for deterministic building
#if 0
	#define LOG_CRC( Array ) UE_LOG( LogStaticMesh, Log, TEXT(#Array " CRC %u"), FCrc::MemCrc32( Array.GetData(), Array.Num() * Array.GetTypeSize() ) )
#else
	#define LOG_CRC( Array )
#endif

namespace Nanite
{

struct FClusterGroup
{
	FSphere3f	Bounds;
	FSphere3f	LODBounds;
	float		ParentLODError		= 0.0f;
	int32		MipLevel			= 0;
	uint32		MeshIndex			= MAX_uint32;
	uint32		AssemblyPartIndex	= MAX_uint32;
	bool		bTrimmed			= false;
	bool		bRoot				= false;

	FPageRangeKey			PageRangeKey;
	TArray< FClusterRef >	Children;
};

struct FAssemblyPartData
{
	uint32 FirstInstance = MAX_uint32;
	uint32 NumInstances = 0;
};

struct FAssemblyInstanceData
{
	FMatrix44f	Transform;
	FSphere3f	LODBounds;			// TODO Index the referencing group instead of copying its properties.
	float		ParentLODError;
	uint32		RootClusterIndex;
	uint32		RootGroupIndex;
	uint32		FirstBoneInfluence;
	uint32		NumBoneInfluences;
};

class FClusterDAG
{
public:
				FClusterDAG() {}
	
	void		AddMesh(
		const FConstMeshBuildVertexView& Verts,
		TArrayView< const uint32 > Indexes,
		TArrayView< const int32 > MaterialIndexes,
		const FBounds3f& VertexBounds,
		const FVertexFormat& VertexFormat );

	void		AddEmptyMesh()	{ MeshInput.AddDefaulted(); }

	void		ReduceMesh( uint32 MeshIndex );

	void		FindCut(
		TArray< FClusterRef >&	SelectedClusters,
		TSet< uint32 >&			SelectedGroups,
		uint32 TargetNumTris,
		float  TargetError,
		uint32 TargetOvershoot ) const;

	TArray< FCluster >		Clusters;
	TArray< FClusterGroup >	Groups;

	TArray< FAssemblyPartData >		AssemblyPartData;
	TArray< FAssemblyInstanceData >	AssemblyInstanceData;
	TArray< FVector2f > 			AssemblyBoneInfluences;

	TArray< TArray< FClusterRef >, TInlineAllocator<1> >	MeshInput;

#if RAY_TRACE_VOXELS
	// TODO Always creates
	FRayTracingScene		RayTracingScene;
#endif

	FBounds3f	TotalBounds;
	float		SurfaceArea = 0.0f;

	struct FSettings
	{
		uint32						NumRays				= 1;
		int32						VoxelLevel			= 0;
		uint32						ExtraVoxelLevels	= 0;
		float						RayBackUp			= 0.0f;
		float						MaxEdgeLengthFactor	= 0.0f;
		ENaniteShapePreservation	ShapePreservation	= ENaniteShapePreservation::None;
		bool						bLerpUVs			: 1 = true;
		bool						bSeparable			: 1 = false;
		bool						bVoxelNDF			: 1 = true;
		bool						bVoxelOpacity		: 1 = false;
	};
	FSettings	Settings;

	bool bHasSkinning		: 1		= false;
	bool bHasTangents		: 1		= false;
	bool bHasColors			: 1		= false;

	uint8 MaxTexCoords		= 0;
	uint8 MaxBoneInfluences	= 0;

private:
	uint32	FindAdjacentClusters( TArray< TMap< uint32, uint32 > >& OutAdjacency, TArrayView< const FClusterRef > LevelClusters, uint32 NumExternalEdges );
	void	GroupTriangleClusters( TArrayView< const FClusterRef > LevelClusters, uint32 NumExternalEdges );
	void	GroupVoxelClusters( TArrayView< const FClusterRef > LevelClusters );

public:	//TEMP
	void		ReduceGroup(
		std::atomic< int32 >& NumClusters,
		uint32 MaxClusterSize,
		uint32 NumParents,
		int32 GroupIndex,
		uint32 MeshIndex );
};

FORCEINLINE FCluster& FClusterRef::GetCluster( FClusterDAG& DAG ) const
{
	return DAG.Clusters[ ClusterIndex ];
}

FORCEINLINE const FCluster& FClusterRef::GetCluster( const FClusterDAG& DAG ) const
{
	return DAG.Clusters[ ClusterIndex ];
}

FORCEINLINE const FMatrix44f& FClusterRef::GetTransform( const FClusterDAG& DAG ) const
{
	return DAG.AssemblyInstanceData[ InstanceIndex ].Transform;
}

} // namespace Nanite