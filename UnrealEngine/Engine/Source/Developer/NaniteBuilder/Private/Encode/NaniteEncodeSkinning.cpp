// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeSkinning.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "NaniteDefinitions.h"
#include "Async/ParallelFor.h"


namespace Nanite
{

static_assert(sizeof(FClusterBoneInfluence) % 4 == 0, "sizeof(FClusterBoneInfluence) must be a multiple of 4");			// shader assumes multiple of 4
static_assert(sizeof(FPackedVoxelBoneInfluence) % 4 == 0, "sizeof(FPackedVoxelBoneInfluence) must be a multiple of 4");

// Carefully quantize a set of weights while making sure their sum hits an exact target.
// If the input weights are in non-ascending order, the output weights will also be in non-ascending order.
template<typename TGetWeight, typename TArrayType>
void QuantizeWeights(const uint32 N, const uint32 TargetTotalQuantizedWeight, TArrayType& QuantizedWeights, TGetWeight&& GetWeight)
{
	float TotalWeight = 0.0f;
	for (uint32 i = 0; i < N; i++)
	{
		TotalWeight += (float)GetWeight(i);
	}

	if (FMath::IsNearlyZero(TotalWeight))
	{
		// bail early on zero total weight
		QuantizedWeights.SetNumZeroed(N);
		return;
	}

	struct FHeapEntry
	{
		float Error;
		uint32 Index;
	};

	TArray<FHeapEntry, TInlineAllocator<64>> ErrorHeap;
	QuantizedWeights.SetNum(N);
	
	uint32 TotalQuantizedWeight = 0;
	for (uint32 i = 0; i < N; i++)
	{
		const float Weight = ((float)GetWeight(i) * (float)TargetTotalQuantizedWeight) / TotalWeight;
		const uint32 QuantizedWeight = FMath::RoundToInt(Weight);
		QuantizedWeights[i] = QuantizedWeight;
		ErrorHeap.Emplace(FHeapEntry{ (float)QuantizedWeight - Weight, i });
		TotalQuantizedWeight += QuantizedWeight;
	}

	if (TotalQuantizedWeight != TargetTotalQuantizedWeight)
	{
		// If the weights don't add up to TargetTotalQuantizedWeight exactly, iteratively increment/decrement the weight that introduces the smallest error.
		const bool bTooSmall = (TotalQuantizedWeight < TargetTotalQuantizedWeight);
		const int32 Diff = bTooSmall ? 1 : -1;

		auto Predicate = [bTooSmall](const FHeapEntry& A, const FHeapEntry& B)
			{
				if (bTooSmall)
				{
					return (A.Error != B.Error) ? (A.Error < B.Error) : (A.Index < B.Index);
				}
				else
				{
					return (A.Error != B.Error) ? (A.Error > B.Error) : (A.Index > B.Index);
				}
			};

		ErrorHeap.Heapify(Predicate);
			
		while (TotalQuantizedWeight != TargetTotalQuantizedWeight)
		{
			check(ErrorHeap.Num() > 0);
			FHeapEntry Entry;
			ErrorHeap.HeapPop(Entry, Predicate, EAllowShrinking::No);
				
			QuantizedWeights[Entry.Index] += Diff;
			TotalQuantizedWeight += Diff;
		}
	}

#if DO_CHECK
	uint32 WeightSum = 0;
	for (uint32 i = 0; i < N; i++)
	{
		uint32 Weight = QuantizedWeights[i];
		check(Weight <= TargetTotalQuantizedWeight);
		WeightSum += Weight;
	}
	check(WeightSum == TargetTotalQuantizedWeight);
#endif
}

void CalculateInfluences(FBoneInfluenceInfo& InfluenceInfo, const FCluster& Cluster, int32 BoneWeightPrecision)
{
	const uint32 NumClusterVerts	= Cluster.Verts.Num();
	const uint32 MaxBones			= Cluster.Verts.Format.NumBoneInfluences;

	if (MaxBones == 0)
		return;

	const bool bVoxel = (Cluster.NumTris == 0);

	uint32	MaxVertexInfluences		= 0;
	uint32	MaxBoneIndex			= 0;
	bool	bClusterBoneOverflow	= false;

	InfluenceInfo.ClusterBoneInfluences.Reserve(NANITE_MAX_CLUSTER_BONE_INFLUENCES);

	TMap<uint32, float> TotalBoneWeightMap;
	
	TArray<uint32, TInlineAllocator<NANITE_MAX_CLUSTER_BONE_INFLUENCES>> NumBoneInfluenceRefs;
	NumBoneInfluenceRefs.SetNum(NANITE_MAX_CLUSTER_BONE_INFLUENCES);

	for (uint32 i = 0; i < NumClusterVerts; i++)
	{
		const FVector3f  LocalPosition	= Cluster.Verts.GetPosition(i);
		const FVector2f* BoneInfluences = Cluster.Verts.GetBoneInfluences(i);

		uint32 NumVertexInfluences = 0;
		for (uint32 j = 0; j < MaxBones; j++)
		{
			const uint32 BoneIndex	= (uint32)BoneInfluences[j].X;
			const float fBoneWeight = BoneInfluences[j].Y;
			const uint32 BoneWeight = FMath::RoundToInt(fBoneWeight);

			// Have we reached the end of weights?
			if (BoneWeight == 0)
			{
				break;
			}

			if (bVoxel)
			{
				TotalBoneWeightMap.FindOrAdd(BoneIndex) += fBoneWeight;
			}
			
			if (!bClusterBoneOverflow)
			{
				// Have we seen this bone index already?
				bool bFound = false;
				for (uint32 InfluenceIndex = 0; InfluenceIndex < (uint32)InfluenceInfo.ClusterBoneInfluences.Num(); InfluenceIndex++)
				{
					FClusterBoneInfluence& ClusterBoneInfluence = InfluenceInfo.ClusterBoneInfluences[InfluenceIndex];

					if (ClusterBoneInfluence.BoneIndex == BoneIndex)
					{
						NumBoneInfluenceRefs[InfluenceIndex]++;

						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					if (InfluenceInfo.ClusterBoneInfluences.Num() < NANITE_MAX_CLUSTER_BONE_INFLUENCES)
					{
						NumBoneInfluenceRefs[InfluenceInfo.ClusterBoneInfluences.Num()]++;

						FClusterBoneInfluence ClusterBoneInfluence;
						ClusterBoneInfluence.BoneIndex = BoneIndex;
						InfluenceInfo.ClusterBoneInfluences.Add(ClusterBoneInfluence);
					}
					else
					{
						// Bones don't fit. Don't bother storing any of them and just revert back to instance bounds
						bClusterBoneOverflow = true;
						InfluenceInfo.ClusterBoneInfluences.Empty();
					}
				}
			}
			
			if (!bVoxel)
			{
				MaxBoneIndex = FMath::Max(MaxBoneIndex, BoneIndex);
				NumVertexInfluences++;
			}
		}
		MaxVertexInfluences = FMath::Max(MaxVertexInfluences, NumVertexInfluences);
	}

	if (TotalBoneWeightMap.Num() > 0)
	{
		// Pick the bones with the largest total influence
		struct FBoneInfluence
		{
			uint32 Bone;
			float Weight;
		};

		TArray<FBoneInfluence, TInlineAllocator<64>> SortedInfluences;
		SortedInfluences.Reserve(TotalBoneWeightMap.Num());
		for (const auto& Pair : TotalBoneWeightMap)
		{
			SortedInfluences.Emplace(FBoneInfluence{ Pair.Key, Pair.Value });
		}

		SortedInfluences.Sort([](const FBoneInfluence& A, const FBoneInfluence& B)
			{
				return A.Weight > B.Weight;
			});

		const uint32 NumElements = (uint32)FMath::Min(SortedInfluences.Num(), NANITE_MAX_VOXEL_ANIMATION_BONE_INFLUENCES);
		
		const uint32 TargetTotalQuantizedWeight = 255;

		// Quantize weights to 8 bits
		TArray<uint32, TInlineAllocator<64>> QuantizedWeights;
		QuantizeWeights(NumElements, TargetTotalQuantizedWeight, QuantizedWeights,
			[&SortedInfluences](uint32 Index)
			{
				return SortedInfluences[Index].Weight;
			});

		InfluenceInfo.VoxelBoneInfluences.Reserve(NumElements);
		for (uint32 i = 0; i < NumElements; i++)
		{
			const uint32 Weight = QuantizedWeights[i];

			if (Weight > 0)
			{
				const uint32 Weight_BoneIndex = Weight | (SortedInfluences[i].Bone << 8);
				InfluenceInfo.VoxelBoneInfluences.Add(FPackedVoxelBoneInfluence{ Weight_BoneIndex });
			}
		}
	}

	// Pick dominant bone per brick
    // TODO: Optimize me
	const uint32 NumBricks = Cluster.Bricks.Num();
	if(NumBricks > 0)
	{
		TMap<uint32, float> BoneWeightMap;
		InfluenceInfo.BrickBoneIndices.SetNum(NumBricks);

		for (uint32 BrickIndex = 0; BrickIndex < NumBricks; BrickIndex++)
		{
			const FCluster::FBrick& Brick = Cluster.Bricks[BrickIndex];
			const uint32 NumVerts = FMath::CountBits(Brick.VoxelMask);
			
			BoneWeightMap.Reset();
		
			for (uint32 i = 0; i < NumVerts; i++)
			{
				const FVector2f* BoneInfluences = Cluster.Verts.GetBoneInfluences(Brick.VertOffset + i);

				for (uint32 j = 0; j < MaxBones; j++)
				{
					const uint32 BoneIndex = (uint32)BoneInfluences[j].X;
					const float fBoneWeight = BoneInfluences[j].Y;
					const uint32 BoneWeight = FMath::RoundToInt(fBoneWeight);

					BoneWeightMap.FindOrAdd(BoneIndex) += fBoneWeight;
				}
			}
		
			float BestWeight = -MAX_flt;
			uint32 BestBoneIndex = MAX_uint32;
			for (const auto& Pair : BoneWeightMap)
			{
				if (Pair.Value > BestWeight)
				{
					BestWeight = Pair.Value;
					BestBoneIndex = Pair.Key;
				}
			}
			check(BestBoneIndex != MAX_uint32);

			InfluenceInfo.BrickBoneIndices[BrickIndex] = BestBoneIndex;
		}
	}
	
	InfluenceInfo.NumVertexBoneInfluences	= MaxVertexInfluences;
	InfluenceInfo.NumVertexBoneIndexBits	= FMath::CeilLogTwo(MaxBoneIndex + 1u);
	InfluenceInfo.NumVertexBoneWeightBits	= MaxVertexInfluences > 1 ? BoneWeightPrecision : 0u;	// Drop bone weights if only one bone is used
}

void PackBoneInfluenceHeader(FPackedBoneInfluenceHeader& PackedBoneInfluenceHeader, const FBoneInfluenceInfo& BoneInfluenceInfo)
{
	PackedBoneInfluenceHeader = FPackedBoneInfluenceHeader();
	PackedBoneInfluenceHeader.SetDataOffset(BoneInfluenceInfo.DataOffset);
	PackedBoneInfluenceHeader.SetNumVertexInfluences(BoneInfluenceInfo.NumVertexBoneInfluences);
	PackedBoneInfluenceHeader.SetNumVertexBoneIndexBits(BoneInfluenceInfo.NumVertexBoneIndexBits);
	PackedBoneInfluenceHeader.SetNumVertexBoneWeightBits(BoneInfluenceInfo.NumVertexBoneWeightBits);
}

static void QuantizeBoneWeights(FCluster& Cluster, int32 BoneWeightPrecision)
{
	const uint32 NumVerts			= Cluster.Verts.Num();
	const uint32 NumBoneInfluences	= Cluster.Verts.Format.NumBoneInfluences;
	
	const uint32 TargetTotalBoneWeight = BoneWeightPrecision ? ((1u << BoneWeightPrecision) - 1u) : 1u;
	
	for (uint32 VertIndex = 0; VertIndex < NumVerts; VertIndex++)
	{
		FVector2f* BoneInfluences = Cluster.Verts.GetBoneInfluences(VertIndex);

		QuantizeAndSortBoneInfluenceWeights(TArrayView<FVector2f>(BoneInfluences, NumBoneInfluences), TargetTotalBoneWeight);
	}
}

void QuantizeAndSortBoneInfluenceWeights(TArrayView<FVector2f> BoneInfluences, uint32 TargetTotalQuantizedWeight)
{
	const uint32 NumBoneInfluences = BoneInfluences.Num();
	TArray<uint32, TInlineAllocator<64>> QuantizedWeights;
	QuantizeWeights(NumBoneInfluences, TargetTotalQuantizedWeight, QuantizedWeights, [BoneInfluences](uint32 Index) -> float
		{
			return BoneInfluences[Index].Y;
		});

	for (uint32 i = 0; i < NumBoneInfluences; ++i)
	{
		BoneInfluences[i].Y = (float)QuantizedWeights[i];
		
		if (QuantizedWeights[i] == 0)
		{
			BoneInfluences[i].X = 0.0f;	// Clear index when weight is 0
		}
	}

	// Sort just to be sure. Maybe the input was not in non-ascending order.
	BoneInfluences.Sort([](const FVector2f& A, const FVector2f& B) { return A.Y > B.Y || (A.Y == B.Y && A.X > B.X); });
}

void QuantizeBoneWeights(TArray<FCluster>& Clusters, int32 BoneWeightPrecision)
{
	ParallelFor(TEXT("NaniteEncode.QuantizeBoneWeights.PF"), Clusters.Num(), 256,
		[&Clusters, BoneWeightPrecision](uint32 ClusterIndex)
		{
			QuantizeBoneWeights(Clusters[ClusterIndex], BoneWeightPrecision);
		});
}


} // namespace Nanite
