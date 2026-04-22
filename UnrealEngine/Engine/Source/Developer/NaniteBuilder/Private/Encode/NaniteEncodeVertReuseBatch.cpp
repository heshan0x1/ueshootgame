// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteEncodeVertReuseBatch.h"

#include "Math/UnrealMath.h"
#include "Cluster.h"
#include "NaniteDefinitions.h"
#include "Async/ParallelFor.h"
#include "Containers/StaticBitArray.h"

namespace Nanite
{

uint32 CalcVertReuseBatchInfoSize(const TArrayView<const FMaterialRange>& MaterialRanges)
{
	constexpr int32 NumBatchCountBits = 4;
	constexpr int32 NumTriCountBits = 5;
	constexpr int32 WorstCaseFullBatchTriCount = 10;

	int32 TotalNumBatches = 0;
	int32 NumBitsNeeded = 0;

	for (const FMaterialRange& MaterialRange : MaterialRanges)
	{
		const int32 NumBatches = MaterialRange.BatchTriCounts.Num();
		check(NumBatches > 0 && NumBatches < (1 << NumBatchCountBits));
		TotalNumBatches += NumBatches;
		NumBitsNeeded += NumBatchCountBits + NumBatches * NumTriCountBits;
	}
	NumBitsNeeded += FMath::Max(NumBatchCountBits * (3 - MaterialRanges.Num()), 0);
	check(TotalNumBatches < FMath::DivideAndRoundUp(NANITE_MAX_CLUSTER_TRIANGLES, WorstCaseFullBatchTriCount) + MaterialRanges.Num() - 1);

	return FMath::DivideAndRoundUp(NumBitsNeeded, 32);
}

void PackVertReuseBatchInfo(const TArrayView<const FMaterialRange>& MaterialRanges, TArray<uint32>& OutVertReuseBatchInfo)
{
	constexpr int32 NumBatchCountBits = 4;
	constexpr int32 NumTriCountBits = 5;

	//TODO: Could we just use BitWriter here?
	auto AppendBits = [](uint32*& DwordPtr, uint32& BitOffset, uint32 Bits, uint32 NumBits)
	{
		uint32 BitsConsumed = FMath::Min(NumBits, 32u - BitOffset);
		SetBits(*DwordPtr, (Bits & ((1 << BitsConsumed) - 1)), BitsConsumed, BitOffset);
		BitOffset += BitsConsumed;
		if (BitOffset >= 32u)
		{
			check(BitOffset == 32u);
			++DwordPtr;
			BitOffset -= 32u;
		}
		if (BitsConsumed < NumBits)
		{
			Bits >>= BitsConsumed;
			BitsConsumed = NumBits - BitsConsumed;
			SetBits(*DwordPtr, Bits, BitsConsumed, BitOffset);
			BitOffset += BitsConsumed;
			check(BitOffset < 32u);
		}
	};

	const uint32 NumDwordsNeeded = CalcVertReuseBatchInfoSize(MaterialRanges);
	OutVertReuseBatchInfo.Empty(NumDwordsNeeded);
	OutVertReuseBatchInfo.AddZeroed(NumDwordsNeeded);

	uint32* NumArrayDwordPtr = &OutVertReuseBatchInfo[0];
	uint32 NumArrayBitOffset = 0;
	const uint32 NumArrayBits = FMath::Max(MaterialRanges.Num(), 3) * NumBatchCountBits;
	uint32* TriCountDwordPtr = &OutVertReuseBatchInfo[NumArrayBits >> 5];
	uint32 TriCountBitOffset = NumArrayBits & 0x1f;

	for (const FMaterialRange& MaterialRange : MaterialRanges)
	{
		const uint32 NumBatches = MaterialRange.BatchTriCounts.Num();
		check(NumBatches > 0);
		AppendBits(NumArrayDwordPtr, NumArrayBitOffset, NumBatches, NumBatchCountBits);

		for (int32 BatchIndex = 0; BatchIndex < MaterialRange.BatchTriCounts.Num(); ++BatchIndex)
		{
			const uint32 BatchTriCount = MaterialRange.BatchTriCounts[BatchIndex];
			check(BatchTriCount > 0 && BatchTriCount - 1 < (1 << NumTriCountBits));
			AppendBits(TriCountDwordPtr, TriCountBitOffset, BatchTriCount - 1, NumTriCountBits);
		}
	}
}

static void BuildVertReuseBatches(FCluster& Cluster)
{
	for (FMaterialRange& MaterialRange : Cluster.MaterialRanges)
	{
		TStaticBitArray<NANITE_MAX_CLUSTER_VERTICES> UsedVertMask;
		uint32 NumUniqueVerts = 0;
		uint32 NumTris = 0;
		const uint32 MaxBatchVerts = 32;
		const uint32 MaxBatchTris = 32;
		const uint32 TriIndexEnd = MaterialRange.RangeStart + MaterialRange.RangeLength;

		MaterialRange.BatchTriCounts.Reset();

		for (uint32 TriIndex = MaterialRange.RangeStart; TriIndex < TriIndexEnd; ++TriIndex)
		{
			const uint32 VertIndex0 = Cluster.Indexes[TriIndex * 3 + 0];
			const uint32 VertIndex1 = Cluster.Indexes[TriIndex * 3 + 1];
			const uint32 VertIndex2 = Cluster.Indexes[TriIndex * 3 + 2];

			auto Bit0 = UsedVertMask[VertIndex0];
			auto Bit1 = UsedVertMask[VertIndex1];
			auto Bit2 = UsedVertMask[VertIndex2];

			// If adding this tri to the current batch will result in too many unique verts, start a new batch
			const uint32 NumNewUniqueVerts = uint32(!Bit0) + uint32(!Bit1) + uint32(!Bit2);
			if (NumUniqueVerts + NumNewUniqueVerts > MaxBatchVerts)
			{
				check(NumTris > 0);
				MaterialRange.BatchTriCounts.Add(uint8(NumTris));
				NumUniqueVerts = 0;
				NumTris = 0;
				UsedVertMask = TStaticBitArray<NANITE_MAX_CLUSTER_VERTICES>();
				--TriIndex;
				continue;
			}

			Bit0 = true;
			Bit1 = true;
			Bit2 = true;
			NumUniqueVerts += NumNewUniqueVerts;
			++NumTris;

			if (NumTris == MaxBatchTris)
			{
				MaterialRange.BatchTriCounts.Add(uint8(NumTris));
				NumUniqueVerts = 0;
				NumTris = 0;
				UsedVertMask = TStaticBitArray<NANITE_MAX_CLUSTER_VERTICES>();
			}
		}

		if (NumTris > 0)
		{
			MaterialRange.BatchTriCounts.Add(uint8(NumTris));
		}
	}
}

void BuildVertReuseBatches(TArray<FCluster>& Clusters)
{
	ParallelFor(TEXT("NaniteEncode.BuildVertReuseBatches.PF"), Clusters.Num(), 256,
		[&Clusters](uint32 ClusterIndex)
		{
			if( Clusters[ ClusterIndex ].NumTris )
				BuildVertReuseBatches(Clusters[ClusterIndex]);
		});
}

} // namespace Nanite
