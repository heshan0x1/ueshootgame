// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NaniteDefinitions.h"
#include "Math/Bounds.h"

namespace Nanite
{

class FCluster;

struct FPageSections
{
	uint32 Cluster				= 0;
	uint32 ClusterBoneInfluence = 0;
	uint32 VoxelBoneInfluence	= 0;
	uint32 MaterialTable		= 0;
	uint32 VertReuseBatchInfo	= 0;
	uint32 BoneInfluence		= 0;
	uint32 BrickData			= 0;
	uint32 ExtendedData			= 0;
	uint32 DecodeInfo			= 0;
	uint32 Index				= 0;
	uint32 Position				= 0;
	uint32 Attribute			= 0;

	uint32 GetClusterBoneInfluenceSize() const	{ return Align(ClusterBoneInfluence, 16); }
	uint32 GetVoxelBoneInfluenceSize() const	{ return Align(VoxelBoneInfluence, 16); }
	uint32 GetMaterialTableSize() const			{ return Align(MaterialTable, 16); }
	uint32 GetVertReuseBatchInfoSize() const	{ return Align(VertReuseBatchInfo, 16); }
	uint32 GetBoneInfluenceSize() const			{ return Align(BoneInfluence, 16); }
	uint32 GetBrickDataSize() const				{ return Align(BrickData, 16); }
	uint32 GetExtendedDataSize() const			{ return Align(ExtendedData, 16); }
	uint32 GetDecodeInfoSize() const			{ return Align(DecodeInfo, 16); }

	uint32 GetClusterOffset() const				{ return NANITE_GPU_PAGE_HEADER_SIZE; }
	uint32 GetClusterBoneInfluenceOffset() const{ return GetClusterOffset() + Cluster; }
	uint32 GetVoxelBoneInfluenceOffset() const	{ return GetClusterBoneInfluenceOffset() + GetClusterBoneInfluenceSize(); }
	uint32 GetMaterialTableOffset() const		{ return GetVoxelBoneInfluenceOffset() + GetVoxelBoneInfluenceSize(); }
	uint32 GetVertReuseBatchInfoOffset() const	{ return GetMaterialTableOffset() + GetMaterialTableSize(); }
	uint32 GetBoneInfluenceOffset() const		{ return GetVertReuseBatchInfoOffset() + GetVertReuseBatchInfoSize(); }
	uint32 GetBrickDataOffset() const			{ return GetBoneInfluenceOffset() + GetBoneInfluenceSize(); }
	uint32 GetExtendedDataOffset() const		{ return GetBrickDataOffset() + GetBrickDataSize(); }
	uint32 GetDecodeInfoOffset() const			{ return GetExtendedDataOffset() + GetExtendedDataSize(); } 
	uint32 GetIndexOffset() const				{ return GetDecodeInfoOffset() + GetDecodeInfoSize(); }
	uint32 GetPositionOffset() const			{ return GetIndexOffset() + Index; }
	uint32 GetAttributeOffset() const			{ return GetPositionOffset() + Position; }
	uint32 GetTotal() const						{ return GetAttributeOffset() + Attribute; }

	FPageSections GetOffsets() const
	{
		return FPageSections
		{
			GetClusterOffset(),
			GetClusterBoneInfluenceOffset(),
			GetVoxelBoneInfluenceOffset(),
			GetMaterialTableOffset(),
			GetVertReuseBatchInfoOffset(),
			GetBoneInfluenceOffset(),
			GetBrickDataOffset(),
			GetExtendedDataOffset(),
			GetDecodeInfoOffset(),
			GetIndexOffset(),
			GetPositionOffset(),
			GetAttributeOffset()
		};
	}

	void operator+=(const FPageSections& Other)
	{
		Cluster				+=	Other.Cluster;
		ClusterBoneInfluence+=	Other.ClusterBoneInfluence;
		VoxelBoneInfluence	+=	Other.VoxelBoneInfluence;
		MaterialTable		+=	Other.MaterialTable;
		VertReuseBatchInfo	+=	Other.VertReuseBatchInfo;
		BoneInfluence		+=	Other.BoneInfluence;
		BrickData			+=	Other.BrickData;
		ExtendedData		+=	Other.ExtendedData;
		DecodeInfo			+=	Other.DecodeInfo;
		Index				+=	Other.Index;
		Position			+=	Other.Position;
		Attribute			+=	Other.Attribute;
	}
};

struct FPageStreams
{
	TArray<uint32>	StripBitmask;
	TArray<uint32>	PageClusterPair;
	TArray<uint32>	VertexRefBitmask;
	TArray<uint16>	VertexRef;
	TArray<uint8>	Index;
	TArray<uint8>	Attribute;
	TArray<uint8>	BoneInfluence;
	TArray<uint8>	Brick;
	TArray<uint32>	Extended;
	TArray<uint32>	MaterialRange;
	TArray<uint32>	VertReuseBatchInfo;
	TArray<uint8>	LowByte;
	TArray<uint8>	MidByte;
	TArray<uint8>	HighByte;
};

struct FPage
{
	uint32	PartsStartIndex				= 0;
	uint32	PartsNum					= 0;
	uint32	NumClusters					= 0;
	uint32	MaxHierarchyPartDepth		= 0;	// Max depth of referenced parts
	uint32	MaxHierarchyDepth			= 0;	// Depth to guarantee next level can also be reached
	uint32	MaxClusterBoneInfluences	= 0;
	uint32	MaxVoxelBoneInfluences		= 0;
	bool	bRelativeEncoding			= false;

	FPageSections	GpuSizes;
};

struct FHierarchyNodeRef
{
	uint32	NodeIndex	= MAX_uint32;
	uint32	ChildIndex	= MAX_uint32;
};

struct FClusterGroupPart
{
	TArray<uint32>	Clusters;
	FBounds3f		Bounds;
	float			MinLODError			= MAX_flt;
	uint32			PageIndex			= MAX_uint32;
	uint32			GroupIndex			= MAX_uint32;
	uint32			PageClusterOffset	= MAX_uint32;

	TArray<FHierarchyNodeRef> HierarchyNodeRefs;
};

struct FUVInfo
{
	FUintVector2 Min = FUintVector2::ZeroValue;
	FUintVector2 NumBits = FUintVector2::ZeroValue;
};

struct FPackedUVHeader
{
	FUintVector2 Data;
};

struct FClusterBoneInfluence
{
	uint32		BoneIndex;
};

struct FPackedVoxelBoneInfluence
{
	uint32		Weight_BoneIndex;	// Weight: 8, BoneIndex: 24
};

struct FBoneInfluenceInfo
{
	uint32	DataOffset = 0;
	uint32	NumVertexBoneInfluences = 0;
	uint32	NumVertexBoneIndexBits = 0;
	uint32	NumVertexBoneWeightBits = 0;

	TArray<uint32> BrickBoneIndices;

	TArray<FClusterBoneInfluence> ClusterBoneInfluences;
	TArray<FPackedVoxelBoneInfluence> VoxelBoneInfluences;
};

struct FPackedBrick
{
	uint32		VoxelMask[2];
	uint32		PositionAndBrickMax[2];	// MaxX: 2, MaxY: 2, MaxZ: 2, PosX: 19, PosY: 19, PosZ: 19
	uint32		VertOffset_BoneIndex;
};

struct FEncodingInfo
{
	uint32				BitsPerIndex = 0;
	uint32				BitsPerAttribute = 0;

	uint32				NormalPrecision = 0;
	uint32				TangentPrecision = 0;
	
	uint32				ColorMode = 0;
	FIntVector4			ColorMin = FIntVector4(0, 0, 0, 0);
	FIntVector4			ColorBits = FIntVector4(0, 0, 0, 0);

	FUVInfo				UVs[NANITE_MAX_UVS];
	FBoneInfluenceInfo	BoneInfluence;

	FPageSections		GpuSizes;
};

class FBitWriter
{
public:
	FBitWriter(TArray<uint8>& Buffer) :
		Buffer(Buffer),
		PendingBits(0ull),
		NumPendingBits(0)
	{
	}

	void PutBits(uint32 Bits, uint32 NumBits)
	{
		check((uint64)Bits < (1ull << NumBits));
		PendingBits |= (uint64)Bits << NumPendingBits;
		NumPendingBits += NumBits;

		while (NumPendingBits >= 8)
		{
			Buffer.Add((uint8)PendingBits);
			PendingBits >>= 8;
			NumPendingBits -= 8;
		}
	}

	void Flush(uint32 Alignment=1)
	{
		if (NumPendingBits > 0)
			Buffer.Add((uint8)PendingBits);
		while (Buffer.Num() % Alignment != 0)
			Buffer.Add(0);
		PendingBits = 0;
		NumPendingBits = 0;
	}

private:
	TArray<uint8>& 	Buffer;
	uint64 			PendingBits;
	int32 			NumPendingBits;
};


template<typename TLambda>
void ProcessPageClusters(const FPage& Page, const TArray<FClusterGroupPart>& Parts, TLambda&& Lambda)
{
	uint32 LocalClusterIndex = 0;
	for (uint32 PartIndex = 0; PartIndex < Page.PartsNum; PartIndex++)
	{
		const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + PartIndex];
		for (uint32 i = 0; i < (uint32)Part.Clusters.Num(); i++)
		{
			Lambda(LocalClusterIndex, Part.Clusters[i]);
			LocalClusterIndex++;
		}
	}
	check(LocalClusterIndex == Page.NumClusters);
}

}
