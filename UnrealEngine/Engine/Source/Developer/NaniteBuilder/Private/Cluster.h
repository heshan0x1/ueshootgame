// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Math/Bounds.h"
#include "MeshSimplify.h"
#include "TriangleUtil.h"
#include "SGGX.h"

#define RAY_TRACE_VOXELS	1

class FGraphPartitioner;

namespace Nanite
{

class FCluster;
class FClusterDAG;
class FRayTracingScene;
struct FRayTracingFallbackBuildSettings;

class FClusterRef
{
public:
	uint32	InstanceIndex;
	uint32	ClusterIndex;

	explicit FClusterRef( uint32 InClusterIndex )
		: InstanceIndex( MAX_uint32 )	
		, ClusterIndex( InClusterIndex )
	{}
	explicit FClusterRef( uint32 InInstanceIndex, uint32 InClusterIndex )
		: InstanceIndex( InInstanceIndex )	
		, ClusterIndex( InClusterIndex )
	{}

	bool	IsInstance() const { return InstanceIndex != MAX_uint32; }

	FCluster&			GetCluster( FClusterDAG& DAG ) const;
	const FCluster&		GetCluster( const FClusterDAG& DAG ) const;
	const FMatrix44f&	GetTransform( const FClusterDAG& DAG ) const;
};

struct FVertexFormat
{
	uint8	NumTexCoords		= 0;
	uint8	NumBoneInfluences	= 0;
	bool	bHasTangents		: 1 = false;
	bool	bHasColors			: 1 = false;

	FORCEINLINE bool Matches( const FVertexFormat& Other ) const
	{
		return
			NumTexCoords		== Other.NumTexCoords &&
			NumBoneInfluences	== Other.NumBoneInfluences &&
			bHasTangents		== Other.bHasTangents &&
			bHasColors			== Other.bHasColors;
	}

	FORCEINLINE uint8 GetColorOffset() const
	{
		return 6 + ( bHasTangents ? 4 : 0 );
	}

	FORCEINLINE uint8 GetUVOffset() const
	{
		return GetColorOffset() + ( bHasColors ? 4 : 0 );
	}

	FORCEINLINE uint8 GetBoneInfluenceOffset() const
	{
		return GetUVOffset() + (NumTexCoords * 2);
	}

	FORCEINLINE uint8 GetVertSize() const
	{
		return GetBoneInfluenceOffset() + (NumBoneInfluences * 2);
	}
};

class FVertexArray
{
public:
	TArray< float >	Array;
	FVertexFormat	Format;

private:
	uint32	NumVerts = 0;
	uint8	ColorOffset;
	uint8	UVOffset;
	uint8	BoneInfluenceOffset;
	uint8	VertSize;

public:
			FVertexArray() = default;
			FVertexArray( const FVertexFormat& InFormat );

	void 	InitFormat();
	void	CopyAttributes( uint32 DstVertIndex, uint32 SrcVertIndex, const FVertexArray& SrcVerts );
	void	LerpAttributes( uint32 DstVertIndex, const FUintVector3& SrcVertIndexes, const FVertexArray& SrcVerts, const FVector3f& Barycentrics );
	uint32	FindOrAddHash( const float* Vert, uint32 VertIndex, FHashTable& HashTable );
	void	Sanitize();

	uint32	AddUninitialized( uint32 Count = 1 );
	uint32	Add( const void* Vert, uint32 Count = 1 );
	void	RemoveAt( uint32 VertIndex, uint32 Count = 1 );

	FORCEINLINE uint8	GetVertSize() const								{ return VertSize; }
	FORCEINLINE uint32	Num() const										{ return NumVerts; }
	FORCEINLINE void	Reserve( uint32 Count )							{ Array.Reserve( Count * VertSize ); }
	FORCEINLINE void	SetNum( uint32 Count )							{ Array.SetNum( Count * VertSize ); NumVerts = Count; }
	
	FORCEINLINE FVector3f&			GetPosition(		uint32 VertIndex )			{ return *Fetch< FVector3f		>( VertIndex, 0 ); }
	FORCEINLINE float*				GetAttributes(		uint32 VertIndex )			{ return  Fetch< float			>( VertIndex, 3 ); }
	FORCEINLINE FVector3f&			GetNormal(			uint32 VertIndex )			{ return *Fetch< FVector3f		>( VertIndex, 3 ); }
	FORCEINLINE FVector3f&			GetTangentX(		uint32 VertIndex )			{ return *Fetch< FVector3f		>( VertIndex, 6 ); }
	FORCEINLINE float&				GetTangentYSign(	uint32 VertIndex )			{ return *Fetch< float			>( VertIndex, 9 ); }
	FORCEINLINE FLinearColor&		GetColor(			uint32 VertIndex )			{ return *Fetch< FLinearColor	>( VertIndex, ColorOffset ); }
	FORCEINLINE FVector2f*			GetUVs(				uint32 VertIndex )			{ return  Fetch< FVector2f		>( VertIndex, UVOffset ); }
	FORCEINLINE FVector2f*			GetBoneInfluences(	uint32 VertIndex )			{ return  Fetch< FVector2f		>( VertIndex, BoneInfluenceOffset ); }

	FORCEINLINE const FVector3f&	GetPosition(		uint32 VertIndex ) const	{ return *Fetch< FVector3f		>( VertIndex, 0 ); }
	FORCEINLINE const float*		GetAttributes(		uint32 VertIndex ) const	{ return  Fetch< float			>( VertIndex, 3 ); }
	FORCEINLINE const FVector3f&	GetNormal(			uint32 VertIndex ) const	{ return *Fetch< FVector3f		>( VertIndex, 3 ); }
	FORCEINLINE const FVector3f&	GetTangentX(		uint32 VertIndex ) const	{ return *Fetch< FVector3f		>( VertIndex, 6 ); }
	FORCEINLINE const float&		GetTangentYSign(	uint32 VertIndex ) const	{ return *Fetch< float			>( VertIndex, 9 ); }
	FORCEINLINE const FLinearColor&	GetColor(			uint32 VertIndex ) const	{ return *Fetch< FLinearColor	>( VertIndex, ColorOffset ); }
	FORCEINLINE const FVector2f*	GetUVs(				uint32 VertIndex ) const	{ return  Fetch< FVector2f		>( VertIndex, UVOffset ); }
	FORCEINLINE const FVector2f*	GetBoneInfluences(	uint32 VertIndex ) const	{ return  Fetch< FVector2f		>( VertIndex, BoneInfluenceOffset ); }

private:
	template< typename T >
	FORCEINLINE T* Fetch( uint32 VertIndex, uint32 Offset )
	{
		return reinterpret_cast< T* >( Array.GetData() + VertIndex * VertSize + Offset );
	}

	template< typename T >
	FORCEINLINE const T* Fetch( uint32 VertIndex, uint32 Offset ) const
	{
		return reinterpret_cast< const T* >( Array.GetData() + VertIndex * VertSize + Offset );
	}
};

FORCEINLINE	FVertexArray::FVertexArray( const FVertexFormat& InFormat )
	: Format( InFormat )
{
	InitFormat();
}

FORCEINLINE void FVertexArray::InitFormat()
{
	ColorOffset			= Format.GetColorOffset();
	UVOffset			= Format.GetUVOffset();
	BoneInfluenceOffset	= Format.GetBoneInfluenceOffset();
	VertSize			= Format.GetVertSize();
}

FORCEINLINE uint32 FVertexArray::AddUninitialized( uint32 Count )
{
	uint32 NewIndex = NumVerts;
	Array.AddUninitialized( Count * VertSize );
	NumVerts += Count;
	return NewIndex;
}

FORCEINLINE uint32 FVertexArray::Add( const void* Verts, uint32 Count )
{
	uint32 NewIndex = AddUninitialized( Count );
	FMemory::Memcpy( Array.GetData() + NewIndex * VertSize, Verts, Count * VertSize * sizeof( float ) );
	return NewIndex;
}

FORCEINLINE void FVertexArray::RemoveAt( uint32 VertIndex, uint32 Count )
{
	Array.RemoveAt( VertIndex * VertSize, Count * VertSize, EAllowShrinking::No );
	NumVerts -= Count;
}

struct FMaterialRange
{
	uint32 RangeStart;
	uint32 RangeLength;
	uint32 MaterialIndex;
	TArray<uint8, TInlineAllocator<12>> BatchTriCounts;

	friend FArchive& operator<<(FArchive& Ar, FMaterialRange& Range);
};

struct FStripDesc
{
	uint32 Bitmasks[4][3];
	uint32 NumPrevRefVerticesBeforeDwords;
	uint32 NumPrevNewVerticesBeforeDwords;

	friend FArchive& operator<<(FArchive& Ar, FStripDesc& Desc);
};


class FCluster
{
public:
	FCluster() {}
	FCluster(
		const FConstMeshBuildVertexView& InVerts,
		TArrayView< const uint32 > InIndexes,
		TArrayView< const int32 > InMaterialIndexes,
		const FVertexFormat& InFormat,
		uint32 Begin, uint32 End,
		TArrayView< const uint32 > SortedIndexes,
		TArrayView< const uint32 > SortedTo,
		const FAdjacency& Adjacency );

	FCluster(
		FCluster& SrcCluster,
		uint32 Begin, uint32 End,
		TArrayView< const uint32 > SortedIndexes,
		TArrayView< const uint32 > SortedTo,
		const FAdjacency& Adjacency );

	FCluster( const FClusterDAG& DAG, TArrayView< const FClusterRef > Children );

	float		Simplify( const FClusterDAG& DAG, uint32 TargetNumTris, float TargetError = 0.0f, uint32 LimitNumTris = 0, const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings = nullptr );
	FAdjacency	BuildAdjacency() const;
	void		Split( FGraphPartitioner& Partitioner, const FAdjacency& Adjacency ) const;
	void		Bound();
	void		Voxelize( FClusterDAG& DAG, TArrayView< const FClusterRef > Children, float VoxelSize );
	void		BuildMaterialRanges();

private:
				template< typename TTransformFunc >
	uint32		AddVertMismatched( uint32 SrcVertIndex, const FCluster& SrcCluster, FHashTable& HashTable, TTransformFunc&& TransformFunc );

	void		CopyAttributes( uint32 DstVertIndex, uint32 SrcVertIndex, const FCluster& SrcCluster );
	void		LerpAttributes( uint32 DstVertIndex, uint32 SrcTriIndex, const FCluster& SrcCluster, const FVector3f& Barycentrics );
	void		VoxelsToBricks( TMap< FIntVector3, uint32 >& VoxelMap );

public:
	friend FArchive& operator<<(FArchive& Ar, FCluster& Cluster);

	static const uint32	ClusterSize = 128;

	uint32				NumTris = 0;

	FVertexArray		Verts;
	TArray< uint32 >	Indexes;
	TArray< int32 >		MaterialIndexes;

#if RAY_TRACE_VOXELS
	TArray< FIntVector3 >	ExtraVoxels;
#endif

	TArray< int8 >		ExternalEdges;
	uint32				NumExternalEdges = 0;

	TArray< uint32 >	ExtendedData;

	// whether the triangle is a proxy for a voxel/brick
	TBitArray<>			VoxelTriangle;
	bool				bHasVoxelTriangles = false;

	struct FBrick
	{
		uint64		VoxelMask;
		FIntVector3	Position;
		uint32		VertOffset;
	};
	TArray< FBrick >	Bricks;

	FBounds3f	Bounds;
	uint64		GUID = 0;
	int32		MipLevel = 0;

	FIntVector	QuantizedPosStart		= { 0u, 0u, 0u };
	int32		QuantizedPosPrecision	= 0u;
	FIntVector  QuantizedPosBits		= { 0u, 0u, 0u };

	float		EdgeLength = 0.0f;
	float		LODError = 0.0f;
	float		SurfaceArea = 0.0f;
	
	FSphere3f	SphereBounds;
	FSphere3f	LODBounds;

	uint32		GroupIndex			= MAX_uint32;
	uint32		GeneratingGroupIndex= MAX_uint32;
	uint32		PageIndex			= MAX_uint32;

	TArray<FMaterialRange, TInlineAllocator<4>> MaterialRanges;
	TArray<FIntVector>	QuantizedPositions;

	FStripDesc		StripDesc;
	TArray<uint8>	StripIndexData;
};

struct FClusterAreaWeightedTriangleSampler : FWeightedRandomSampler
{
	FClusterAreaWeightedTriangleSampler();
	void Init(const FCluster* InCluster);

protected:

	virtual float GetWeights(TArray<float>& OutWeights)override;

	const FCluster* Cluster;
};

} // namespace Nanite