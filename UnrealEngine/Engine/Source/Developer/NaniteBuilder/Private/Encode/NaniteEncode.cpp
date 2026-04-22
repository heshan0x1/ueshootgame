// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncode.h"

#include "Rendering/NaniteResources.h"
#include "NaniteIntermediateResources.h"
#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "Async/ParallelFor.h"
#include "NaniteEncodeShared.h"
#include "NaniteEncodeConstrain.h"
#include "NaniteEncodeHierarchy.h"
#include "NaniteEncodeMaterial.h"
#include "NaniteEncodeFixup.h"
#include "NaniteEncodeSkinning.h"
#include "NaniteEncodePageAssignment.h"
#include "NaniteEncodeGeometryData.h"
#include "NaniteEncodeVertReuseBatch.h"

#define MAX_DEPENDENCY_CHAIN_FOR_RELATIVE_ENCODING	6		// Reset dependency chain by forcing direct encoding every time a page has this many levels of dependent relative encodings.
															// This prevents long chains of dependent dispatches during decode.
															// As this affects only a small fraction of pages, the compression impact is negligible.

#define FLT_INT_MIN									(-2147483648.0f)	// Smallest float >= INT_MIN
#define FLT_INT_MAX									2147483520.0f		// Largest float <= INT_MAX

namespace Nanite
{

struct FPageGPUHeader
{
	uint32	NumClusters_MaxClusterBoneInfluences_MaxVoxelBoneInfluences = 0;		// NumClusters: 16, MaxClusterBoneInfluences: 8, MaxVoxelBoneInfluences: 8
	uint32	Pad[3] = { 0 };

	void	SetNumClusters(uint32 N)				{ SetBits(NumClusters_MaxClusterBoneInfluences_MaxVoxelBoneInfluences, N, 16,  0); }
	void	SetMaxClusterBoneInfluences(uint32 N)	{ SetBits(NumClusters_MaxClusterBoneInfluences_MaxVoxelBoneInfluences, N,  8, 16); }
	void	SetMaxVoxelBoneInfluences(uint32 N)		{ SetBits(NumClusters_MaxClusterBoneInfluences_MaxVoxelBoneInfluences, N,  8, 24); }
};

struct FPageDiskHeader
{
	uint32 NumClusters;
	uint32 NumRawFloat4s;
	uint32 NumVertexRefs;
	uint32 StripBitmaskOffset;
	uint32 VertexRefBitmaskOffset;
};

struct FClusterDiskHeader
{
	uint32 DecodeInfoOffset;
	uint32 IndexDataOffset;
	uint32 PageClusterMapOffset;
	uint32 VertexRefDataOffset;
	uint32 LowBytesOffset;
	uint32 MidBytesOffset;
	uint32 HighBytesOffset;
	uint32 NumVertexRefs;
	uint32 NumPrevRefVerticesBeforeDwords;
	uint32 NumPrevNewVerticesBeforeDwords;
};

static void PackUVHeader(FPackedUVHeader& PackedUVHeader, const FUVInfo& UVInfo)
{
	check(UVInfo.NumBits.X <= NANITE_UV_FLOAT_MAX_BITS			&& UVInfo.NumBits.Y <= NANITE_UV_FLOAT_MAX_BITS);
	check(UVInfo.Min.X     <  (1u << NANITE_UV_FLOAT_MAX_BITS)	&& UVInfo.Min.Y     <  (1u << NANITE_UV_FLOAT_MAX_BITS));
	
	PackedUVHeader.Data.X = (UVInfo.Min.X << 5) | UVInfo.NumBits.X;
	PackedUVHeader.Data.Y = (UVInfo.Min.Y << 5) | UVInfo.NumBits.Y;
}

// Min inclusive, Max exclusive
static void BlockBounds( uint64 BlockBits, FIntVector3& OutMin, FIntVector3& OutMax )
{
	check(BlockBits != 0);
	OutMin.Z = (uint32)FMath::CountTrailingZeros64( BlockBits ) >> 4;
	OutMax.Z = 4u - ( (uint32)FMath::CountLeadingZeros64( BlockBits ) >> 4 );

	uint32 Bits = uint32( BlockBits ) | uint32( BlockBits >> 32 );
	Bits = (Bits | (Bits << 16));
	OutMin.Y = (uint32)FMath::CountTrailingZeros( Bits >> 16 ) >> 2;
	OutMax.Y = 4u - ( (uint32)FMath::CountLeadingZeros( Bits ) >> 2 );
	
	Bits = (Bits | (Bits << 8));
	Bits = (Bits | (Bits << 4));
	OutMin.X = (uint32)FMath::CountTrailingZeros( Bits >> 28 );
	OutMax.X = 4u - (uint32)FMath::CountLeadingZeros( Bits );
	
	check( OutMin.X >= 0 && OutMin.X <= 3 );
	check( OutMin.Y >= 0 && OutMin.Y <= 3 );
	check( OutMin.Z >= 0 && OutMin.Z <= 3 );

	check( OutMax.X >= 1 && OutMax.X <= 4 );
	check( OutMax.Y >= 1 && OutMax.Y <= 4 );
	check( OutMax.Z >= 1 && OutMax.Z <= 4 );
}

static void PackBrick( FPackedBrick& PackedBrick, const FCluster::FBrick& Brick, uint32 BoneIndex )
{
	PackedBrick = {};
	PackedBrick.VoxelMask[0] = ReverseBits( uint32( Brick.VoxelMask >> 32 ) );
	PackedBrick.VoxelMask[1] = ReverseBits( uint32( Brick.VoxelMask ) );
	
	const int PosBits = 19;
	const int PosMask = (1 << PosBits) - 1;
	const int PosMin = -( 1 << ( PosBits - 1 ) );
	const int PosMax =  ( 1 << ( PosBits - 1 ) ) - 1;
	check( Brick.Position.X >= PosMin && Brick.Position.X <= PosMax );
	check( Brick.Position.Y >= PosMin && Brick.Position.Y <= PosMax );
	check( Brick.Position.Z >= PosMin && Brick.Position.Z <= PosMax );
	
	FIntVector3 BlockMin, BlockMax;
	BlockBounds( Brick.VoxelMask, BlockMin, BlockMax );
	
	PackedBrick.PositionAndBrickMax[0]	=	( BlockMax.X - 1 ) | ( ( BlockMax.Y - 1 ) << 2 ) | ( ( BlockMax.Z - 1 ) << 4 ) |
											( ( Brick.Position.X & PosMask ) << 6 ) | ( ( Brick.Position.Y & PosMask ) << 25 );
	PackedBrick.PositionAndBrickMax[1]	=	( ( Brick.Position.Y & PosMask ) >> 7 ) | ( ( Brick.Position.Z & PosMask ) << 12 );

	check(Brick.VertOffset < 0x10000u);
	check(BoneIndex < 0x10000u);
	PackedBrick.VertOffset_BoneIndex	=	Brick.VertOffset | (BoneIndex << 16);
}


static void PackCluster(FPackedCluster& OutCluster, const FCluster& InCluster, const FEncodingInfo& EncodingInfo, bool bHasTangents, uint32 NumTexCoords)
{
	const bool bVoxel = (InCluster.NumTris == 0);

	FMemory::Memzero(OutCluster);

	// 0
	OutCluster.SetNumVerts(InCluster.Verts.Num());
	OutCluster.SetPositionOffset(0);
	OutCluster.SetNumTris(InCluster.NumTris);
	OutCluster.SetIndexOffset(0);
	OutCluster.ColorMin = EncodingInfo.ColorMin.X | (EncodingInfo.ColorMin.Y << 8) | (EncodingInfo.ColorMin.Z << 16) | (EncodingInfo.ColorMin.W << 24);
	OutCluster.SetColorBitsR(EncodingInfo.ColorBits.X);
	OutCluster.SetColorBitsG(EncodingInfo.ColorBits.Y);
	OutCluster.SetColorBitsB(EncodingInfo.ColorBits.Z);
	OutCluster.SetColorBitsA(EncodingInfo.ColorBits.W);
	OutCluster.SetGroupIndex(InCluster.GroupIndex);

	// 1
	OutCluster.PosStart = InCluster.QuantizedPosStart;
	OutCluster.SetBitsPerIndex(EncodingInfo.BitsPerIndex);
	OutCluster.SetPosPrecision(InCluster.QuantizedPosPrecision);
	OutCluster.SetPosBitsX(InCluster.QuantizedPosBits.X);
	OutCluster.SetPosBitsY(InCluster.QuantizedPosBits.Y);
	OutCluster.SetPosBitsZ(InCluster.QuantizedPosBits.Z);

	// 2
	OutCluster.LODBounds				= InCluster.LODBounds;

	// 3
	OutCluster.BoxBoundsCenter			= (InCluster.Bounds.Min + InCluster.Bounds.Max) * 0.5f;
	OutCluster.LODErrorAndEdgeLength	= FFloat16(InCluster.LODError).Encoded | (FFloat16(InCluster.EdgeLength).Encoded << 16);

	// 4
	OutCluster.BoxBoundsExtent			= (InCluster.Bounds.Max - InCluster.Bounds.Min) * 0.5f;
	OutCluster.SetFlags(NANITE_CLUSTER_FLAG_STREAMING_LEAF | NANITE_CLUSTER_FLAG_ROOT_LEAF);
	OutCluster.SetNumClusterBoneInfluences(bVoxel ? EncodingInfo.BoneInfluence.VoxelBoneInfluences.Num() :
		EncodingInfo.BoneInfluence.ClusterBoneInfluences.Num());
	
	// 5
	check(NumTexCoords <= NANITE_MAX_UVS);
	static_assert(NANITE_MAX_UVS <= 4, "UV_Prev encoding only supports up to 4 channels");

	uint32 UVBitOffsets = 0;
	uint32 BitOffset = 0;
	for (uint32 i = 0; i < NumTexCoords; i++)
	{
		check(BitOffset < 256);
		UVBitOffsets |= BitOffset << (i * 8);
		const FUVInfo& UVInfo = EncodingInfo.UVs[i];
		BitOffset += UVInfo.NumBits.X + UVInfo.NumBits.Y;
	}

	// 6
	OutCluster.SetBitsPerAttribute(EncodingInfo.BitsPerAttribute);
	OutCluster.SetNormalPrecision(EncodingInfo.NormalPrecision);
	OutCluster.SetTangentPrecision(EncodingInfo.TangentPrecision);
	OutCluster.SetHasTangents(bHasTangents);
	OutCluster.SetNumUVs(NumTexCoords);
	OutCluster.SetColorMode(EncodingInfo.ColorMode);
	OutCluster.UVBitOffsets			= UVBitOffsets;
	OutCluster.PackedMaterialInfo	= 0;	// Filled out by WritePages

}

static int32 CalculateQuantizedPositionsUniformGrid(TArray< FCluster >& Clusters, const FMeshNaniteSettings& Settings)
{
	// Simple global quantization for EA
	const int32 MaxPositionQuantizedValue	= (1 << NANITE_MAX_POSITION_QUANTIZATION_BITS) - 1;

	{
		// Make sure the worst case bounding box fits with the position encoding settings. Ideally this would be a compile-time check.
		const float MaxValue = FMath::RoundToFloat(NANITE_MAX_COORDINATE_VALUE * FMath::Exp2((float)NANITE_MIN_POSITION_PRECISION));
		checkf(MaxValue <= FLT_INT_MAX && int64(MaxValue) - int64(-MaxValue) <= MaxPositionQuantizedValue, TEXT("Largest cluster bounds doesn't fit in position bits"));
	}
	
	int32 PositionPrecision = Settings.PositionPrecision;
	if (PositionPrecision == MIN_int32)
	{
		// Heuristic: We want higher resolution if the mesh is denser.
		// Use geometric average of cluster size as a proxy for density.
		// Alternative interpretation: Bit precision is average of what is needed by the clusters.
		// For roughly uniformly sized clusters this gives results very similar to the old quantization code.
		double TotalLogSize = 0.0;
		int32 TotalNum = 0;
		for (const FCluster& Cluster : Clusters)
		{
			if (Cluster.MipLevel == 0 && Cluster.NumTris != 0)
			{
				float ExtentSize = Cluster.Bounds.GetExtent().Size();
				if (ExtentSize > 0.0)
				{
					TotalLogSize += FMath::Log2(ExtentSize);
					TotalNum++;
				}
			}
		}
		double AvgLogSize = TotalNum > 0 ? TotalLogSize / TotalNum : 0.0;
		PositionPrecision = 7 - (int32)FMath::RoundToInt(AvgLogSize);

		// Clamp precision. The user now needs to explicitly opt-in to the lowest precision settings.
		// These settings are likely to cause issues and contribute little to disk size savings (~0.4% on test project),
		// so we shouldn't pick them automatically.
		// Example: A very low resolution road or building frame that needs little precision to look right in isolation,
		// but still requires fairly high precision in a scene because smaller meshes are placed on it or in it.
		const int32 AUTO_MIN_PRECISION = 4;	// 1/16cm
		PositionPrecision = FMath::Max(PositionPrecision, AUTO_MIN_PRECISION);
	}

	PositionPrecision = FMath::Clamp(PositionPrecision, NANITE_MIN_POSITION_PRECISION, NANITE_MAX_POSITION_PRECISION);

	float QuantizationScale = FMath::Exp2((float)PositionPrecision);

	// Make sure all clusters are encodable. A large enough cluster could hit the 21bpc limit. If it happens scale back until it fits.
	for (const FCluster& Cluster : Clusters)
	{
		if (Cluster.NumTris == 0)
		{
			continue;
		}

		const FBounds3f& Bounds = Cluster.Bounds;
		
		int32 Iterations = 0;
		while (true)
		{
			float MinX = FMath::RoundToFloat(Bounds.Min.X * QuantizationScale);
			float MinY = FMath::RoundToFloat(Bounds.Min.Y * QuantizationScale);
			float MinZ = FMath::RoundToFloat(Bounds.Min.Z * QuantizationScale);

			float MaxX = FMath::RoundToFloat(Bounds.Max.X * QuantizationScale);
			float MaxY = FMath::RoundToFloat(Bounds.Max.Y * QuantizationScale);
			float MaxZ = FMath::RoundToFloat(Bounds.Max.Z * QuantizationScale);

			if (MinX >= FLT_INT_MIN && MinY >= FLT_INT_MIN && MinZ >= FLT_INT_MIN &&
				MaxX <= FLT_INT_MAX && MaxY <= FLT_INT_MAX && MaxZ <= FLT_INT_MAX &&
				((int64)MaxX - (int64)MinX) <= MaxPositionQuantizedValue && ((int64)MaxY - (int64)MinY) <= MaxPositionQuantizedValue && ((int64)MaxZ - (int64)MinZ) <= MaxPositionQuantizedValue)
			{
				break;
			}
			
			QuantizationScale *= 0.5f;
			PositionPrecision--;
			check(PositionPrecision >= NANITE_MIN_POSITION_PRECISION);
			check(++Iterations < 100);	// Endless loop?
		}
	}

	const float RcpQuantizationScale = 1.0f / QuantizationScale;

	ParallelFor( TEXT("NaniteEncode.QuantizeClusterPositions.PF"), Clusters.Num(), 256, [&](uint32 ClusterIndex)
	{
		FCluster& Cluster = Clusters[ClusterIndex];

		if (Cluster.NumTris == 0)
		{
			return;
		}
		
		const uint32 NumClusterVerts = Cluster.Verts.Num();
		Cluster.QuantizedPositions.SetNumUninitialized(NumClusterVerts);

		// Quantize positions
		FIntVector IntClusterMax = { MIN_int32,	MIN_int32, MIN_int32 };
		FIntVector IntClusterMin = { MAX_int32,	MAX_int32, MAX_int32 };

		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			const FVector3f Position = Cluster.Verts.GetPosition(i);

			FIntVector& IntPosition = Cluster.QuantizedPositions[i];
			float PosX = FMath::RoundToFloat(Position.X * QuantizationScale);
			float PosY = FMath::RoundToFloat(Position.Y * QuantizationScale);
			float PosZ = FMath::RoundToFloat(Position.Z * QuantizationScale);

			IntPosition = FIntVector((int32)PosX, (int32)PosY, (int32)PosZ);

			IntClusterMax.X = FMath::Max(IntClusterMax.X, IntPosition.X);
			IntClusterMax.Y = FMath::Max(IntClusterMax.Y, IntPosition.Y);
			IntClusterMax.Z = FMath::Max(IntClusterMax.Z, IntPosition.Z);
			IntClusterMin.X = FMath::Min(IntClusterMin.X, IntPosition.X);
			IntClusterMin.Y = FMath::Min(IntClusterMin.Y, IntPosition.Y);
			IntClusterMin.Z = FMath::Min(IntClusterMin.Z, IntPosition.Z);
		}

		// Store in minimum number of bits
		const uint32 NumBitsX = FMath::CeilLogTwo(IntClusterMax.X - IntClusterMin.X + 1);
		const uint32 NumBitsY = FMath::CeilLogTwo(IntClusterMax.Y - IntClusterMin.Y + 1);
		const uint32 NumBitsZ = FMath::CeilLogTwo(IntClusterMax.Z - IntClusterMin.Z + 1);
		check(NumBitsX <= NANITE_MAX_POSITION_QUANTIZATION_BITS);
		check(NumBitsY <= NANITE_MAX_POSITION_QUANTIZATION_BITS);
		check(NumBitsZ <= NANITE_MAX_POSITION_QUANTIZATION_BITS);

		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			FIntVector& IntPosition = Cluster.QuantizedPositions[i];

			// Update float position with quantized data
			Cluster.Verts.GetPosition(i) = FVector3f((float)IntPosition.X * RcpQuantizationScale, (float)IntPosition.Y * RcpQuantizationScale, (float)IntPosition.Z * RcpQuantizationScale);
			
			IntPosition.X -= IntClusterMin.X;
			IntPosition.Y -= IntClusterMin.Y;
			IntPosition.Z -= IntClusterMin.Z;
			check(IntPosition.X >= 0 && IntPosition.X < (1 << NumBitsX));
			check(IntPosition.Y >= 0 && IntPosition.Y < (1 << NumBitsY));
			check(IntPosition.Z >= 0 && IntPosition.Z < (1 << NumBitsZ));
		}


		// Update bounds
		Cluster.Bounds.Min = FVector3f((float)IntClusterMin.X * RcpQuantizationScale, (float)IntClusterMin.Y * RcpQuantizationScale, (float)IntClusterMin.Z * RcpQuantizationScale);
		Cluster.Bounds.Max = FVector3f((float)IntClusterMax.X * RcpQuantizationScale, (float)IntClusterMax.Y * RcpQuantizationScale, (float)IntClusterMax.Z * RcpQuantizationScale);

		Cluster.QuantizedPosBits = FIntVector(NumBitsX, NumBitsY, NumBitsZ);
		Cluster.QuantizedPosStart = IntClusterMin;
		Cluster.QuantizedPosPrecision = PositionPrecision;

	} );
	return PositionPrecision;
}

//TODO: Could we fold this into some other pass now?
static void CalculateMeshBounds(
	FClusterDAG& ClusterDAG,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts,
	FBoxSphereBounds3f& OutFinalBounds
	)
{
	TArray<FCluster>& Clusters = ClusterDAG.Clusters;
	TArray<FClusterGroup>& ClusterGroups = ClusterDAG.Groups;

	OutFinalBounds.Origin = ClusterDAG.TotalBounds.GetCenter();
	OutFinalBounds.BoxExtent = ClusterDAG.TotalBounds.GetExtent();
	OutFinalBounds.SphereRadius = 0.0f;

	// Calculate bounds of instanced group parts
	for (FClusterGroupPart& Part : Parts)
	{
		check(Part.Clusters.Num() <= NANITE_MAX_CLUSTERS_PER_GROUP);
		check(Part.PageIndex < (uint32)Pages.Num());

		const FClusterGroup& Group = ClusterGroups[Part.GroupIndex];
		if (Group.AssemblyPartIndex == INDEX_NONE)
		{
			for (uint32 ClusterIndex : Part.Clusters)
			{
				const FSphere3f SphereBounds = Clusters[ClusterIndex].SphereBounds;
				const float Radius = (SphereBounds.Center - OutFinalBounds.Origin).Length() + SphereBounds.W;
				OutFinalBounds.SphereRadius = FMath::Max(OutFinalBounds.SphereRadius, Radius);
			}
		}
		else
		{
			const FAssemblyPartData& AssemblyPart = ClusterDAG.AssemblyPartData[Group.AssemblyPartIndex];
			for (uint32 TransformIndex = 0; TransformIndex < AssemblyPart.NumInstances; ++TransformIndex)
			{
				// Calculate the bounds of all clusters in their instanced location
				const uint32 AssemblyTransformIndex = AssemblyPart.FirstInstance + TransformIndex;
				const FMatrix44f& Transform = ClusterDAG.AssemblyInstanceData[AssemblyTransformIndex].Transform;

				for (uint32 ClusterIndex : Part.Clusters)
				{
					FSphere3f SphereBounds = Clusters[ClusterIndex].SphereBounds.TransformBy(Transform);
					const float Radius = (SphereBounds.Center - OutFinalBounds.Origin).Length() + SphereBounds.W;
					OutFinalBounds.SphereRadius = FMath::Max(OutFinalBounds.SphereRadius, Radius);
				}
			}
		}
	}
}

class FPageWriter
{
	TArray<uint8>& Bytes;
public:	
	FPageWriter(TArray<uint8>& InBytes) :
		Bytes(InBytes)
	{
	}

	template<typename T>
	T* Append_Ptr(uint32 Num)
	{
		const uint32 SizeBefore = (uint32)Bytes.Num();
		Bytes.AddZeroed(Num * sizeof(T));
		return (T*)(Bytes.GetData() + SizeBefore);
	}
	
	template<typename T>
	uint32 Append_Offset(uint32 Num)
	{
		const uint32 SizeBefore = (uint32)Bytes.Num();
		Bytes.AddZeroed(Num * sizeof(T));
		return SizeBefore;
	}

	template<typename T>
	void Append(const TArray<T>& Data)
	{
		Bytes.Append((uint8*)Data.GetData(), (uint32)Data.NumBytes());
	}

	uint32 Offset() const
	{
		return (uint32)Bytes.Num();
	}

	void AlignRelativeToOffset(uint32 StartOffset, uint32 Alignment)
	{
		check(Offset() >= StartOffset);
		const uint32 Remainder = (Offset() - StartOffset) % Alignment;
		if (Remainder != 0)
		{
			Bytes.AddZeroed(Alignment - Remainder);
		}
	}

	void Align(uint32 Alignment)
	{
		AlignRelativeToOffset(0u, Alignment);	
	}
};

static uint32 MarkRelativeEncodingPagesRecursive(TArray<FPage>& Pages, TArray<uint32>& PageDependentsDepth, const TArray<TArray<uint32>>& PageDependents, uint32 PageIndex)
{
	if (PageDependentsDepth[PageIndex] != MAX_uint32)
	{
		return PageDependentsDepth[PageIndex];
	}

	uint32 Depth = 0;
	for (const uint32 DependentPageIndex : PageDependents[PageIndex])
	{
		const uint32 DependentDepth = MarkRelativeEncodingPagesRecursive(Pages, PageDependentsDepth, PageDependents, DependentPageIndex);
		Depth = FMath::Max(Depth, DependentDepth + 1u);
	}

	FPage& Page = Pages[PageIndex];
	Page.bRelativeEncoding = true;

	if (Depth >= MAX_DEPENDENCY_CHAIN_FOR_RELATIVE_ENCODING)
	{
		// Using relative encoding for this page would make the dependency chain too long. Use direct coding instead and reset depth.
		Page.bRelativeEncoding = false;
		Depth = 0;
	}
	
	PageDependentsDepth[PageIndex] = Depth;
	return Depth;
}

static uint32 MarkRelativeEncodingPages(const FResources& Resources, TArray<FPage>& Pages, const TArray<FClusterGroup>& Groups)
{
	const uint32 NumPages = Resources.PageStreamingStates.Num();

	// Build list of dependents for each page
	TArray<TArray<uint32>> PageDependents;
	PageDependents.SetNum(NumPages);

	// Memorize how many levels of dependency a given page has
	TArray<uint32> PageDependentsDepth;
	PageDependentsDepth.Init(MAX_uint32, NumPages);

	TBitArray<> PageHasOnlyRootDependencies(false, NumPages);

	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];

		bool bHasRootDependency = false;
		bool bHasStreamingDependency = false;
		for (uint32 i = 0; i < PageStreamingState.DependenciesNum; i++)
		{
			const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + i];
			if (Resources.IsRootPage(DependencyPageIndex))
			{
				bHasRootDependency = true;
			}
			else
			{
				PageDependents[DependencyPageIndex].AddUnique(PageIndex);
				bHasStreamingDependency = true;
			}
		}

		PageHasOnlyRootDependencies[PageIndex] = (bHasRootDependency && !bHasStreamingDependency);
	}

	uint32 NumRelativeEncodingPages = 0;
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		FPage& Page = Pages[PageIndex];

		MarkRelativeEncodingPagesRecursive(Pages, PageDependentsDepth, PageDependents, PageIndex);
		
		if (Resources.IsRootPage(PageIndex))
		{
			// Root pages never use relative encoding
			Page.bRelativeEncoding = false;
		}
		else if (PageHasOnlyRootDependencies[PageIndex])
		{
			// Root pages are always resident, so dependencies on them shouldn't count towards dependency chain limit.
			// If a page only has root dependencies, always code it as relative.
			Page.bRelativeEncoding = true;
		}

		if (Page.bRelativeEncoding)
		{
			NumRelativeEncodingPages++;
		}
	}

	return NumRelativeEncodingPages;
}

static void WritePages(
	FResources& Resources,
	TArray<FPage>& Pages,
	const FClusterDAG& ClusterDAG,
	const TArray<FClusterGroupPart>& Parts,
	const TArray<FEncodingInfo>& EncodingInfos,
	const TArray<FPageFixups>& PageFixups,
	const bool bHasSkinning,
	uint32* OutTotalGPUSize)
{
	const TArray<FCluster>& Clusters = ClusterDAG.Clusters;
	const TArray<FClusterGroup>& Groups = ClusterDAG.Groups;

	const uint32 NumPages = Pages.Num();
	
	auto PageVertexMaps = BuildVertexMaps(Pages, Clusters, Parts);

	const uint32 NumRelativeEncodingPages = MarkRelativeEncodingPages(Resources, Pages, Groups);
	
	// Process pages
	TArray< TArray<uint8> > PageResults;
	PageResults.SetNum(NumPages);

	std::atomic<uint64> VoxelMaterialsMask(0);

	ParallelFor(TEXT("NaniteEncode.BuildPages.PF"), NumPages, 1, [&](int32 PageIndex)
	{
		const FPage& Page = Pages[PageIndex];

		Resources.PageStreamingStates[PageIndex].Flags = Page.bRelativeEncoding ? NANITE_PAGE_FLAG_RELATIVE_ENCODING : 0;
		Resources.PageStreamingStates[PageIndex].MaxHierarchyDepth = uint8(Pages[PageIndex].MaxHierarchyDepth);

		TArray<uint16>				CodedVerticesPerCluster;
		TArray<uint32>				NumPageClusterPairsPerCluster;
		TArray<FPackedCluster>		PackedClusters;
		TArray<FPackedBoneInfluenceHeader>	PackedBoneInfluenceHeaders;

		FPageStreams Streams;

		struct FByteStreamCounters
		{
			uint32 Low = 0;
			uint32 Mid = 0;
			uint32 High = 0;
		};

		TArray<FByteStreamCounters> ByteStreamCounters;
		ByteStreamCounters.SetNumUninitialized(Page.NumClusters);

		PackedClusters.SetNumUninitialized(Page.NumClusters);
		CodedVerticesPerCluster.SetNumUninitialized(Page.NumClusters);
		NumPageClusterPairsPerCluster.SetNumUninitialized(Page.NumClusters);

		if(bHasSkinning)
		{
			PackedBoneInfluenceHeaders.SetNumUninitialized(Page.NumClusters);
		}
		
		check(IsAligned(Page.GpuSizes.GetMaterialTableOffset(), 4));
		const uint32 MaterialTableStartOffsetInDwords = Page.GpuSizes.GetMaterialTableOffset() >> 2;

		FPageSections GpuSectionOffsets = Page.GpuSizes.GetOffsets();
		TMap<FVariableVertex, uint32> UniqueVertices;

		uint64 PageVoxelMaterialMask = 0ull;

		ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
		{
			const FCluster& Cluster = Clusters[ClusterIndex];
			const FEncodingInfo& EncodingInfo = EncodingInfos[ClusterIndex];

			FPackedCluster& PackedCluster = PackedClusters[LocalClusterIndex];
			PackCluster(PackedCluster, Cluster, EncodingInfos[ClusterIndex], Cluster.Verts.Format.bHasTangents, Cluster.Verts.Format.NumTexCoords);

			check(IsAligned(GpuSectionOffsets.Index, 4));
			check(IsAligned(GpuSectionOffsets.Position, 4));
			check(IsAligned(GpuSectionOffsets.Attribute, 4));
			PackedCluster.SetIndexOffset(GpuSectionOffsets.Index);
			PackedCluster.SetPositionOffset(GpuSectionOffsets.Position);
			PackedCluster.SetAttributeOffset(GpuSectionOffsets.Attribute);
			PackedCluster.SetDecodeInfoOffset(GpuSectionOffsets.DecodeInfo);
			PackedCluster.SetHasSkinning(bHasSkinning);

			if(bHasSkinning)
			{
				FPackedBoneInfluenceHeader& PackedBoneInfluenceHeader = PackedBoneInfluenceHeaders[LocalClusterIndex];
				PackBoneInfluenceHeader(PackedBoneInfluenceHeader, EncodingInfo.BoneInfluence);
				check(IsAligned(GpuSectionOffsets.BoneInfluence, 4));
				PackedBoneInfluenceHeader.SetDataOffset(GpuSectionOffsets.BoneInfluence);
			}

			if( Cluster.Bricks.Num() > 0 )
			{
				PackedCluster.SetBrickDataOffset( GpuSectionOffsets.BrickData );
				PackedCluster.SetBrickDataNum( Cluster.Bricks.Num() );


				for( uint32 BrickIndex = 0; BrickIndex < (uint32)Cluster.Bricks.Num(); BrickIndex++ )
				{
					const FCluster::FBrick& Brick = Cluster.Bricks[BrickIndex];
					
					FPackedBrick PackedBrick;
					const uint32 BoneIndex = EncodingInfo.BoneInfluence.BrickBoneIndices.Num() ? EncodingInfo.BoneInfluence.BrickBoneIndices[BrickIndex] : 0u;
					PackBrick(PackedBrick, Brick, BoneIndex);
					Streams.Brick.Append( (uint8*)&PackedBrick, sizeof(PackedBrick));
				}
			}


			// No effect if unused
			if( Cluster.ExtendedData.Num() > 0 )
			{
				PackedCluster.SetExtendedDataOffset( GpuSectionOffsets.ExtendedData );
				PackedCluster.SetExtendedDataNum( Cluster.ExtendedData.Num() );
				Streams.Extended.Append( Cluster.ExtendedData );
			}

			PackedCluster.PackedMaterialInfo = PackMaterialInfo(Cluster, Streams.MaterialRange, MaterialTableStartOffsetInDwords);
				
			if( Cluster.NumTris )
			{
				TArray<uint32> LocalVertReuseBatchInfo;
				PackVertReuseBatchInfo(MakeArrayView(Cluster.MaterialRanges), LocalVertReuseBatchInfo);
	
				PackedCluster.SetVertResourceBatchInfo(LocalVertReuseBatchInfo, GpuSectionOffsets.VertReuseBatchInfo, Cluster.MaterialRanges.Num());
				if (Cluster.MaterialRanges.Num() > 3)
				{
					Streams.VertReuseBatchInfo.Append(MoveTemp(LocalVertReuseBatchInfo));
				}
			}

			if( Cluster.NumTris == 0 )
			{
				for( const FMaterialRange& Range : Cluster.MaterialRanges )
				{
					PageVoxelMaterialMask |= 1ull << Range.MaterialIndex;
				}
			}
			
			GpuSectionOffsets += EncodingInfo.GpuSizes;

			const uint32 PrevLow	= Streams.LowByte.Num();
			const uint32 PrevMid	= Streams.MidByte.Num();
			const uint32 PrevHigh	= Streams.HighByte.Num();

			const FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];
			const uint32 DependenciesNum = (PageStreamingState.Flags & NANITE_PAGE_FLAG_RELATIVE_ENCODING) ? PageStreamingState.DependenciesNum : 0u;
			const TArrayView<uint16> PageDependencies = TArrayView<uint16>(Resources.PageDependencies.GetData() + PageStreamingState.DependenciesStart, DependenciesNum);
			const uint32 PrevPageClusterPairs = Streams.PageClusterPair.Num();
			uint32 NumCodedVertices = 0;
			EncodeGeometryData(	LocalClusterIndex, Cluster, EncodingInfo, 
								PageDependencies, PageVertexMaps,
								UniqueVertices, NumCodedVertices, Streams );

			ByteStreamCounters[LocalClusterIndex].Low	= Streams.LowByte.Num() - PrevLow;
			ByteStreamCounters[LocalClusterIndex].Mid	= Streams.MidByte.Num() - PrevMid;
			ByteStreamCounters[LocalClusterIndex].High	= Streams.HighByte.Num() - PrevHigh;

			NumPageClusterPairsPerCluster[LocalClusterIndex] = Streams.PageClusterPair.Num() - PrevPageClusterPairs;
			CodedVerticesPerCluster[LocalClusterIndex] = uint16(NumCodedVertices);
		});
		check(GpuSectionOffsets.Cluster							== Page.GpuSizes.GetClusterBoneInfluenceOffset());
		check(Align(GpuSectionOffsets.MaterialTable, 16)		== Page.GpuSizes.GetVertReuseBatchInfoOffset());
		check(Align(GpuSectionOffsets.VertReuseBatchInfo, 16)	== Page.GpuSizes.GetBoneInfluenceOffset());
		check(Align(GpuSectionOffsets.BoneInfluence, 16)		== Page.GpuSizes.GetBrickDataOffset());
		check(Align(GpuSectionOffsets.BrickData, 16)			== Page.GpuSizes.GetExtendedDataOffset());
		check(Align(GpuSectionOffsets.ExtendedData, 16)			== Page.GpuSizes.GetDecodeInfoOffset());
		check(Align(GpuSectionOffsets.DecodeInfo, 16)			== Page.GpuSizes.GetIndexOffset());
		check(GpuSectionOffsets.Index							== Page.GpuSizes.GetPositionOffset());
		check(GpuSectionOffsets.Position						== Page.GpuSizes.GetAttributeOffset());
		check(GpuSectionOffsets.Attribute						== Page.GpuSizes.GetTotal());

		PerformPageInternalFixup(Resources, Pages, ClusterDAG, Parts, PageIndex, PackedClusters);
		
		VoxelMaterialsMask |= PageVoxelMaterialMask;

		// Begin page
		TArray<uint8>& PageResult = PageResults[PageIndex];
		PageResult.Reset(NANITE_ESTIMATED_MAX_PAGE_DISK_SIZE);

		FPageWriter PageWriter(PageResult);

		// Disk header
		const uint32 PageDiskHeaderOffset = PageWriter.Append_Offset<FPageDiskHeader>(1);

		// 16-byte align material range data to make it easy to copy during GPU transcoding
		Streams.Index.SetNum(Align(Streams.Index.Num(), 4));
		Streams.MaterialRange.SetNum(Align(Streams.MaterialRange.Num(), 4));
		Streams.VertReuseBatchInfo.SetNum(Align(Streams.VertReuseBatchInfo.Num(), 4));
		Streams.BoneInfluence.SetNum(Align(Streams.BoneInfluence.Num(), 16));
		Streams.Brick.SetNum(Align(Streams.Brick.Num(), 16));
		Streams.Extended.SetNum(Align(Streams.Extended.Num(), 4));

		static_assert(sizeof(FPageGPUHeader) % 16 == 0, "sizeof(FGPUPageHeader) must be a multiple of 16");
		static_assert(sizeof(FPackedCluster) % 16 == 0, "sizeof(FPackedCluster) must be a multiple of 16");
		
		// Cluster headers
		const uint32 ClusterDiskHeadersOffset = PageWriter.Append_Offset<FClusterDiskHeader>(Page.NumClusters);
		TArray<FClusterDiskHeader> ClusterDiskHeaders;
		ClusterDiskHeaders.SetNum(Page.NumClusters);

		const uint32 RawFloat4StartOffset = PageWriter.Offset();
		{
			// GPU page header
			FPageGPUHeader& GPUPageHeader = *PageWriter.Append_Ptr<FPageGPUHeader>(1);
			GPUPageHeader = FPageGPUHeader();
			GPUPageHeader.SetNumClusters(Page.NumClusters);
			GPUPageHeader.SetMaxClusterBoneInfluences(Page.MaxClusterBoneInfluences);
			GPUPageHeader.SetMaxVoxelBoneInfluences(Page.MaxVoxelBoneInfluences);
		}

		// Write clusters in SOA layout
		{
			const uint32 NumClusterFloat4Properties = sizeof(FPackedCluster) / 16;
			uint8* Dst = PageWriter.Append_Ptr<uint8>(NumClusterFloat4Properties * 16 * PackedClusters.Num());
			for (uint32 float4Index = 0; float4Index < NumClusterFloat4Properties; float4Index++)
			{
				for (const FPackedCluster& PackedCluster : PackedClusters)
				{
					FMemory::Memcpy(Dst, (uint8*)&PackedCluster + float4Index * 16, 16);
					Dst += 16;
				}
			}
		}

		// Cluster bone data in SOA layout
		{
			const uint32 ClusterBoneInfluenceOffset = PageWriter.Offset();
			FClusterBoneInfluence* Ptr = PageWriter.Append_Ptr<FClusterBoneInfluence>(Page.NumClusters * Page.MaxClusterBoneInfluences);
			
			ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
			{
				const TArray<FClusterBoneInfluence>& ClusterBoneInfluences = EncodingInfos[ClusterIndex].BoneInfluence.ClusterBoneInfluences;

				const uint32 NumInfluences = FMath::Min((uint32)ClusterBoneInfluences.Num(), Page.MaxClusterBoneInfluences);
				for (uint32 i = 0; i < NumInfluences; i++)
				{
					Ptr[Page.NumClusters * i + LocalClusterIndex] = ClusterBoneInfluences[i];
				}
			});

			PageWriter.AlignRelativeToOffset(ClusterBoneInfluenceOffset, 16u);
			check(PageWriter.Offset() - ClusterBoneInfluenceOffset == Page.GpuSizes.GetClusterBoneInfluenceSize());
		}

		// Voxel bone data in SOA layout
		{
			const uint32 VoxelBoneInfluenceOffset = PageWriter.Offset();
			uint32* Ptr = PageWriter.Append_Ptr<uint32>(Page.NumClusters * Page.MaxVoxelBoneInfluences);

			ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
			{
				const TArray<FPackedVoxelBoneInfluence>& VoxelBoneInfluences = EncodingInfos[ClusterIndex].BoneInfluence.VoxelBoneInfluences;

				const uint32 NumInfluences = FMath::Min((uint32)VoxelBoneInfluences.Num(), Page.MaxVoxelBoneInfluences);
				for (uint32 k = 0; k < NumInfluences; k++)
				{
					Ptr[Page.NumClusters * k + LocalClusterIndex] = VoxelBoneInfluences[k].Weight_BoneIndex;
				}
			});
			
			PageWriter.AlignRelativeToOffset(VoxelBoneInfluenceOffset, 16u);
			check(PageWriter.Offset() - VoxelBoneInfluenceOffset == Page.GpuSizes.GetVoxelBoneInfluenceSize());
		}
				
		// Material table
		check((uint32)Streams.MaterialRange.NumBytes() == Page.GpuSizes.GetMaterialTableSize());
		PageWriter.Append(Streams.MaterialRange);
		
		// Vert reuse batch info
		check((uint32)Streams.VertReuseBatchInfo.NumBytes() == Page.GpuSizes.GetVertReuseBatchInfoSize());
		PageWriter.Append(Streams.VertReuseBatchInfo);
		
		// Bone data
		check((uint32)Streams.BoneInfluence.NumBytes() == Page.GpuSizes.GetBoneInfluenceSize());
		PageWriter.Append(Streams.BoneInfluence);
		
		// Brick data
		check((uint32)Streams.Brick.NumBytes() == Page.GpuSizes.GetBrickDataSize());
		PageWriter.Append(Streams.Brick);

		// Extended data
		check((uint32)Streams.Extended.NumBytes() == Page.GpuSizes.GetExtendedDataSize());
		PageWriter.Append(Streams.Extended);
		
		// Decode information
		const uint32 DecodeInfoOffset = PageWriter.Offset();
		ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
		{
			const FCluster& Cluster = Clusters[ClusterIndex];
			
			FClusterDiskHeader& ClusterDiskHeader = ClusterDiskHeaders[LocalClusterIndex];
			ClusterDiskHeader.DecodeInfoOffset = PageWriter.Offset();

			FPackedUVHeader* UVHeaders = PageWriter.Append_Ptr<FPackedUVHeader>(Cluster.Verts.Format.NumTexCoords);

			for (uint32 i = 0; i < Cluster.Verts.Format.NumTexCoords; i++)
			{
				PackUVHeader(UVHeaders[i], EncodingInfos[ClusterIndex].UVs[i]);
			}

			if (bHasSkinning)
			{
				FPackedBoneInfluenceHeader* BoneInfluenceHeader = PageWriter.Append_Ptr<FPackedBoneInfluenceHeader>(1);
				*BoneInfluenceHeader = PackedBoneInfluenceHeaders[LocalClusterIndex];
			}
		});
		
		PageWriter.AlignRelativeToOffset(DecodeInfoOffset, 16u);
		check(PageWriter.Offset() - DecodeInfoOffset == Page.GpuSizes.GetDecodeInfoSize());

		const uint32 RawFloat4EndOffset = PageWriter.Offset();
		
		uint32 StripBitmaskOffset = 0u;
		// Index data
		{
			const uint32 StartOffset = PageWriter.Offset();
			uint32 NextOffset = StartOffset;
#if NANITE_USE_STRIP_INDICES
			ProcessPageClusters(Page, Parts, [&](uint32 LocalClusterIndex, uint32 ClusterIndex)
			{
				const FCluster& Cluster = Clusters[ClusterIndex];

				FClusterDiskHeader& ClusterDiskHeader = ClusterDiskHeaders[LocalClusterIndex];
				ClusterDiskHeader.IndexDataOffset = NextOffset;
				ClusterDiskHeader.NumPrevNewVerticesBeforeDwords = Cluster.StripDesc.NumPrevNewVerticesBeforeDwords;
				ClusterDiskHeader.NumPrevRefVerticesBeforeDwords = Cluster.StripDesc.NumPrevRefVerticesBeforeDwords;
					
				NextOffset += Cluster.StripIndexData.Num();
			});

			const uint32 Size = NextOffset - StartOffset;
			check((uint32)Streams.Index.Num() == Size);
			PageWriter.Append(Streams.Index);
			PageWriter.Align(sizeof(uint32));

			StripBitmaskOffset = PageWriter.Offset();
			PageWriter.Append(Streams.StripBitmask);
			
#else
			for (uint32 i = 0; i < Page.NumClusters; i++)
			{
				ClusterDiskHeaders[i].IndexDataOffset = NextOffset;
				NextOffset += PackedClusters[i].GetNumTris() * 3;
			}
			PageWriter.Align(sizeof(uint32));

			const uint32 Size = NextOffset - StartOffset;
			check(Size == Streams.IndexData.NumBytes());
			PageWriter.Append(Streams.IndexData);
#endif
		}

		// Write PageCluster Map
		{
			const uint32 StartOffset = PageWriter.Offset();
			uint32 NextOffset = StartOffset;
			for (uint32 i = 0; i < Page.NumClusters; i++)
			{
				ClusterDiskHeaders[i].PageClusterMapOffset = NextOffset;
				NextOffset += NumPageClusterPairsPerCluster[i] * sizeof(uint32);
			}
			const uint32 Size = NextOffset - StartOffset;
			check(Streams.PageClusterPair.NumBytes() == Size);
			check(IsAligned(Size, 4));
			PageWriter.Append(Streams.PageClusterPair);
		}

		// Write Vertex Reference Bitmask
		const uint32 VertexRefBitmaskOffset = PageWriter.Offset();
		{
			check(Streams.VertexRefBitmask.NumBytes() == Page.NumClusters * (NANITE_MAX_CLUSTER_VERTICES / 8));
			PageWriter.Append(Streams.VertexRefBitmask);
		}

		// Write Vertex References
		{
			const uint32 StartOffset = PageWriter.Offset();
			uint32 NextOffset = StartOffset;
			for (uint32 i = 0; i < Page.NumClusters; i++)
			{
				const uint32 NumVertexRefs = PackedClusters[i].GetNumVerts() - CodedVerticesPerCluster[i];
				ClusterDiskHeaders[i].VertexRefDataOffset	= NextOffset;
				ClusterDiskHeaders[i].NumVertexRefs			= NumVertexRefs;
				NextOffset += NumVertexRefs;
			}
			const uint32 Size = NextOffset - StartOffset;
			uint8* VertexRefs = PageWriter.Append_Ptr<uint8>(Size * 2); // * 2 to also allocate space for the high bytes that follow
			PageWriter.Align(sizeof(uint32));

			// Split low and high bytes for better compression
			for (int32 i = 0; i < Streams.VertexRef.Num(); i++)
			{
				VertexRefs[i] = Streams.VertexRef[i] >> 8;
				VertexRefs[i + Streams.VertexRef.Num()] = Streams.VertexRef[i] & 0xFF;
			}
		}
		
		// Write low/mid/high byte streams
		{
			const uint32 StartOffset = PageWriter.Offset();
			uint32 NextLowOffset = StartOffset;
			uint32 NextMidOffset = NextLowOffset + Streams.LowByte.Num();
			uint32 NextHighOffset = NextMidOffset + Streams.MidByte.Num();
			for (uint32 i = 0; i < Page.NumClusters; i++)
			{
				ClusterDiskHeaders[i].LowBytesOffset = NextLowOffset;
				ClusterDiskHeaders[i].MidBytesOffset = NextMidOffset;
				ClusterDiskHeaders[i].HighBytesOffset = NextHighOffset;
				NextLowOffset += ByteStreamCounters[i].Low;
				NextMidOffset += ByteStreamCounters[i].Mid;
				NextHighOffset += ByteStreamCounters[i].High;
			}

			const uint32 Size = NextHighOffset - StartOffset;
			check(Size == Streams.LowByte.Num() + Streams.MidByte.Num() + Streams.HighByte.Num());

			PageWriter.Append(Streams.LowByte);
			PageWriter.Append(Streams.MidByte);
			PageWriter.Append(Streams.HighByte);
		}

		const uint32 NumRawFloat4Bytes = RawFloat4EndOffset - RawFloat4StartOffset;
		check(IsAligned(NumRawFloat4Bytes, 16));

		// Write page header
		{
			FPageDiskHeader PageDiskHeader;
			PageDiskHeader.NumClusters = Page.NumClusters;
			PageDiskHeader.NumRawFloat4s = NumRawFloat4Bytes / 16u;
			PageDiskHeader.NumVertexRefs = Streams.VertexRef.Num();
			PageDiskHeader.StripBitmaskOffset = StripBitmaskOffset;
			PageDiskHeader.VertexRefBitmaskOffset = VertexRefBitmaskOffset;
			FMemory::Memcpy(PageResult.GetData() + PageDiskHeaderOffset, &PageDiskHeader, sizeof(PageDiskHeader));
		}

		// Write cluster headers
		FMemory::Memcpy(PageResult.GetData() + ClusterDiskHeadersOffset, ClusterDiskHeaders.GetData(), ClusterDiskHeaders.NumBytes());

		PageWriter.Align(sizeof(uint32));
	});

	Resources.VoxelMaterialsMask = VoxelMaterialsMask;

	// Write pages
	TArray< uint8 > StreamableBulkData;

	uint32 NumRootPages = 0;
	uint32 TotalRootGPUSize = 0;
	uint32 TotalRootDiskSize = 0;
	uint32 NumStreamingPages = 0;
	uint32 TotalStreamingGPUSize = 0;
	uint32 TotalStreamingDiskSize = 0;
	
	uint32 TotalFixupSize = 0;
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const FPage& Page = Pages[PageIndex];
		const bool bRootPage = Resources.IsRootPage(PageIndex);

		TArray<uint8>& BulkData = bRootPage ? Resources.RootData : StreamableBulkData;

		FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];
		PageStreamingState.BulkOffset = BulkData.Num();

		// Write fixup chunk
		TArray<uint8> FixupChunkData;
		BuildFixupChunkData(FixupChunkData, PageFixups[PageIndex], Page.NumClusters);
		BulkData.Append(FixupChunkData.GetData(), FixupChunkData.Num());
		TotalFixupSize += FixupChunkData.Num();

		// Copy page to BulkData
		TArray<uint8>& PageData = PageResults[PageIndex];
		BulkData.Append(PageData.GetData(), PageData.Num());
		
		if (bRootPage)
		{
			TotalRootGPUSize += Page.GpuSizes.GetTotal();
			TotalRootDiskSize += PageData.Num();
			NumRootPages++;
		}
		else
		{
			TotalStreamingGPUSize += Page.GpuSizes.GetTotal();
			TotalStreamingDiskSize += PageData.Num();
			NumStreamingPages++;
		}

		PageStreamingState.BulkSize = BulkData.Num() - PageStreamingState.BulkOffset;
		PageStreamingState.PageSize = PageData.Num();
	}

	const uint32 TotalPageGPUSize = TotalRootGPUSize + TotalStreamingGPUSize;
	const uint32 TotalPageDiskSize = TotalRootDiskSize + TotalStreamingDiskSize;
	UE_LOG(LogStaticMesh, Log, TEXT("WritePages:"), NumPages);
	UE_LOG(LogStaticMesh, Log, TEXT("  Root: GPU size: %d bytes. %d Pages. %.3f bytes per page (%.3f%% utilization)."), TotalRootGPUSize, NumRootPages, (float)TotalRootGPUSize / (float)NumRootPages, (float)TotalRootGPUSize / (float(NumRootPages * NANITE_ROOT_PAGE_GPU_SIZE)) * 100.0f);
	if(NumStreamingPages > 0)
	{
		UE_LOG(LogStaticMesh, Log, TEXT("  Streaming: GPU size: %d bytes. %d Pages (%d with relative encoding). %.3f bytes per page (%.3f%% utilization)."), TotalStreamingGPUSize, NumStreamingPages, NumRelativeEncodingPages, (float)TotalStreamingGPUSize / float(NumStreamingPages), (float)TotalStreamingGPUSize / (float(NumStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE)) * 100.0f);
	}
	else
	{
		UE_LOG(LogStaticMesh, Log, TEXT("  Streaming: 0 bytes."));
	}
	UE_LOG(LogStaticMesh, Log, TEXT("  Page data disk size: %d bytes. Fixup data size: %d bytes."), TotalPageDiskSize, TotalFixupSize);
	UE_LOG(LogStaticMesh, Log, TEXT("  Total GPU size: %d bytes, Total disk size: %d bytes."), TotalPageGPUSize, TotalPageDiskSize + TotalFixupSize);

	// Store PageData
	Resources.StreamablePages.Lock(LOCK_READ_WRITE);
	uint8* Ptr = (uint8*)Resources.StreamablePages.Realloc(StreamableBulkData.Num());
	FMemory::Memcpy(Ptr, StreamableBulkData.GetData(), StreamableBulkData.Num());
	Resources.StreamablePages.Unlock();
	Resources.StreamablePages.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);

	if(OutTotalGPUSize)
	{
		*OutTotalGPUSize = TotalRootGPUSize + TotalStreamingGPUSize;
	}
}

// Remove degenerate triangles
static void RemoveDegenerateTriangles(FCluster& Cluster)
{
	uint32 NumOldTriangles = Cluster.NumTris;
	uint32 NumNewTriangles = 0;

	for (uint32 OldTriangleIndex = 0; OldTriangleIndex < NumOldTriangles; OldTriangleIndex++)
	{
		uint32 i0 = Cluster.Indexes[OldTriangleIndex * 3 + 0];
		uint32 i1 = Cluster.Indexes[OldTriangleIndex * 3 + 1];
		uint32 i2 = Cluster.Indexes[OldTriangleIndex * 3 + 2];
		uint32 mi = Cluster.MaterialIndexes[OldTriangleIndex];

		if (i0 != i1 && i0 != i2 && i1 != i2)
		{
			Cluster.Indexes[NumNewTriangles * 3 + 0] = i0;
			Cluster.Indexes[NumNewTriangles * 3 + 1] = i1;
			Cluster.Indexes[NumNewTriangles * 3 + 2] = i2;
			Cluster.MaterialIndexes[NumNewTriangles] = mi;

			NumNewTriangles++;
		}
	}
	Cluster.NumTris = NumNewTriangles;
	Cluster.Indexes.SetNum(NumNewTriangles * 3);
	Cluster.MaterialIndexes.SetNum(NumNewTriangles);
}

static void RemoveDegenerateTriangles(TArray<FCluster>& Clusters)
{
	ParallelFor(TEXT("NaniteEncode.RemoveDegenerateTriangles.PF"), Clusters.Num(), 512,
		[&]( uint32 ClusterIndex )
		{
			if( Clusters[ ClusterIndex ].NumTris )
				RemoveDegenerateTriangles( Clusters[ ClusterIndex ] );
		} );
}

static uint32 CalculateMaxRootPages(uint32 TargetResidencyInKB)
{
	const uint64 SizeInBytes = uint64(TargetResidencyInKB) << 10;
	return (uint32)FMath::Clamp((SizeInBytes + NANITE_ROOT_PAGE_GPU_SIZE - 1u) >> NANITE_ROOT_PAGE_GPU_SIZE_BITS, 1llu, (uint64)MAX_uint32);
}

static void EncodeAssemblyData(const FClusterDAG& ClusterDAG, FResources& Resources)
{
	const int32 NumTransforms = ClusterDAG.AssemblyInstanceData.Num();
	if (NumTransforms > 0)
	{
		// Encode the transforms into 4x3 transposed
		// TODO: Nanite-Assemblies - Remove shear here by making matrices orthogonal?
		check(NumTransforms <= NANITE_HIERARCHY_MAX_ASSEMBLY_TRANSFORMS); // should have been handled already
		Resources.AssemblyTransforms.SetNumUninitialized(NumTransforms);
		for( int i = 0; i < NumTransforms; i++ )
		{
			TransposeTransform( Resources.AssemblyTransforms[i], ClusterDAG.AssemblyInstanceData[i].Transform );
		}

		if (ClusterDAG.AssemblyBoneInfluences.Num() > 0)
		{
			// Build a look up table and influence data for each instance
			Resources.AssemblyBoneAttachmentData.Reserve(NumTransforms + ClusterDAG.AssemblyBoneInfluences.Num());
			Resources.AssemblyBoneAttachmentData.SetNumUninitialized(NumTransforms);
			for (int i = 0; i < NumTransforms; i++)
			{
				const FAssemblyInstanceData& InstanceData = ClusterDAG.AssemblyInstanceData[i];

				uint32 PackedHeader = InstanceData.NumBoneInfluences << 24u;
				if (InstanceData.NumBoneInfluences == 1)
				{
					// Encode the only bone influence into the lookup entry directly to avoid an unnecessary dependent load in the shader
					const FVector2f& BoneInfluence = ClusterDAG.AssemblyBoneInfluences[InstanceData.FirstBoneInfluence];
					PackedHeader |= (uint32(BoneInfluence.X) & 0xFFFFFFu);
				}
				else if (InstanceData.NumBoneInfluences > 1)
				{
					const uint32 InfluenceOffset = Resources.AssemblyBoneAttachmentData.Num() - NumTransforms;
					PackedHeader |= (InfluenceOffset & 0xFFFFFu);
					
					for (uint32 InfluenceIndex = 0; InfluenceIndex < InstanceData.NumBoneInfluences; ++ InfluenceIndex)
					{
						const FVector2f& BoneInfluence = ClusterDAG.AssemblyBoneInfluences[InstanceData.FirstBoneInfluence + InfluenceIndex];
						Resources.AssemblyBoneAttachmentData.Emplace((uint32(BoneInfluence.X) << 8u) | (uint32(BoneInfluence.Y) & 0xFFu));
					}
				}

				Resources.AssemblyBoneAttachmentData[i] = PackedHeader;
			}
		}
	}
}

void Encode(
	FResources& Resources,
	FClusterDAG& ClusterDAG,
	const FMeshNaniteSettings& Settings,
	uint32 NumMeshes,
	uint32* OutTotalGPUSize
)
{
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::EncodeAssemblyData);
		EncodeAssemblyData( ClusterDAG, Resources );
	}

	// DebugPoisonVertexAttributes(Clusters);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::SanitizeVertexData);
		for (FCluster& Cluster : ClusterDAG.Clusters)
		{
			Cluster.Verts.Sanitize();
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::RemoveDegenerateTriangles);	// TODO: is this still necessary?
		RemoveDegenerateTriangles( ClusterDAG.Clusters );
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::BuildMaterialRanges);
		BuildMaterialRanges( ClusterDAG.Clusters );
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::ConstrainClusters);
		ConstrainClusters( ClusterDAG.Groups, ClusterDAG.Clusters );
	}

#if DO_CHECK
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::VerifyClusterConstraints);
		VerifyClusterConstraints( ClusterDAG.Clusters );
	}
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::BuildVertReuseBatches);
		BuildVertReuseBatches(ClusterDAG.Clusters);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::CalculateQuantizedPositions);
		Resources.PositionPrecision = CalculateQuantizedPositionsUniformGrid( ClusterDAG.Clusters, Settings );	// Needs to happen after clusters have been constrained and split.
	}

	
	int32 BoneWeightPrecision;
	{
		// Select appropriate Auto precision for Normals and Tangents
		// Just use hard-coded defaults for now.
		Resources.NormalPrecision = (Settings.NormalPrecision < 0) ? 8 : FMath::Clamp(Settings.NormalPrecision, 0, NANITE_MAX_NORMAL_QUANTIZATION_BITS);

		if (ClusterDAG.bHasTangents)
		{
			Resources.TangentPrecision = (Settings.TangentPrecision < 0) ? 7 : FMath::Clamp(Settings.TangentPrecision, 0, NANITE_MAX_TANGENT_QUANTIZATION_BITS);
		}
		else
		{
			Resources.TangentPrecision = 0;
		}

		BoneWeightPrecision = (Settings.BoneWeightPrecision < 0) ? 8u : (int32)FMath::Clamp(Settings.BoneWeightPrecision, 0, NANITE_MAX_BLEND_WEIGHT_BITS);
	}

	if (ClusterDAG.bHasSkinning)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::QuantizeBoneWeights);
		QuantizeBoneWeights(ClusterDAG.Clusters, BoneWeightPrecision);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::PrintMaterialRangeStats);
		PrintMaterialRangeStats( ClusterDAG.Clusters );
	}

	TArray<FPage> Pages;
	TArray<FClusterGroupPart> GroupParts;
	TArray<FEncodingInfo> EncodingInfos;
	TArray<FPageFixups> PageFixups;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::CalculateEncodingInfos);
		CalculateEncodingInfos(EncodingInfos, ClusterDAG.Clusters, Resources.NormalPrecision, Resources.TangentPrecision, BoneWeightPrecision);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::AssignClustersToPages);
		const uint32 MaxRootPages = CalculateMaxRootPages(Settings.TargetMinimumResidencyInKB);
		AssignClustersToPages(ClusterDAG, Resources.PageRangeLookup, EncodingInfos, Pages, GroupParts, MaxRootPages);
		Resources.NumRootPages = FMath::Min((uint32)Pages.Num(), MaxRootPages);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::CalculateMeshBounds);
		CalculateMeshBounds(ClusterDAG, Pages, GroupParts, Resources.MeshBounds);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::BuildHierarchyNodes);
		BuildHierarchies(Resources, ClusterDAG, Pages, GroupParts, NumMeshes);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::WritePages);
		CalculatePageDependenciesAndFixups(Resources, PageFixups, Pages, ClusterDAG, GroupParts);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::CalculateFinalPageHierarchyDepth);
		CalculateFinalPageHierarchyDepth(Resources, Pages);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::WritePages);
		WritePages(Resources, Pages, ClusterDAG, GroupParts, EncodingInfos, PageFixups, ClusterDAG.bHasSkinning, OutTotalGPUSize);
	}
}

} // namespace Nanite
