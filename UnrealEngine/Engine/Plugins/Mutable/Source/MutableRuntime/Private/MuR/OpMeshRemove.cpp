// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpMeshRemove.h"

#include "MuR/MeshPrivate.h"
#include "MuR/MutableTrace.h"
#include "MuR/ParallelExecutionUtils.h"
#include "MuR/Platform.h"

#include "Templates/UnrealTemplate.h"
#include "Rendering/MorphTargetVertexCodec.h"

namespace UE::Mutable::Private
{
	void MeshRemoveMeshMorphs(FMesh& Result, TConstArrayView<int32> VertexIndexRemap)
	{
		MUTABLE_CPUPROFILER_SCOPE(CompressedMorphs_MeshRemoveMesh)

		using namespace UE::MorphTargetVertexCodec;

		if (!Result.HasMorphs())
		{
			return;
		}

		auto GetBatchHeader = [](TArrayView<uint32> HeadersDataView, int32 BatchIndex) -> FDeltaBatchHeader
		{
			TArrayView<uint32> HeaderDataView = TArrayView<uint32>(
						HeadersDataView.GetData() + BatchIndex*NumBatchHeaderDwords,
						NumBatchHeaderDwords);

			FDeltaBatchHeader BatchHeader;
			ReadHeader(BatchHeader, HeaderDataView);

			return BatchHeader;
		};

		auto StoreBatchHeader = [](TArrayView<uint32> HeadersDataView, int32 BatchIndex, const FDeltaBatchHeader& BatchHeader) -> void
		{
			TArrayView<uint32> HeaderDataView = TArrayView<uint32>(
						HeadersDataView.GetData() + BatchIndex*NumBatchHeaderDwords,
						NumBatchHeaderDwords);

			WriteHeader(BatchHeader, HeaderDataView);
		};

		int32 NumMorphs = Result.Morph.Names.Num();
		for (int32 MorphIndex = 0; MorphIndex < NumMorphs; ++MorphIndex)
		{
			auto ProcessBatchData =
			[
				VertexIndexRemap
			](
				FDeltaBatchHeader& InOutBatchHeader,
				TArrayView<uint32> DataView,
				int32 BatchVertexRangeBegin,
				int32 BatchVertexRangeEnd
			)
			{
				TArray<FQuantizedDelta, TInlineAllocator<UE::MorphTargetVertexCodec::BatchSize*2>> QuantizedDeltasStorage;

				// Only check the vertices beforehand if the range is within a small range, otherwise 
				// read the quantized deltas and only check the vertices in the batch.
				int32 NumRemovedVerticesInRange = -1;
				constexpr int32 MaxNumVerticesToCheck = 256;
				if (BatchVertexRangeEnd - BatchVertexRangeBegin < MaxNumVerticesToCheck)
				{
					NumRemovedVerticesInRange = 0;
					for (int32 Index = BatchVertexRangeBegin; Index < BatchVertexRangeEnd; ++Index)
					{
						NumRemovedVerticesInRange += static_cast<int32>(VertexIndexRemap[Index] < 0);
					}
				}

				if (NumRemovedVerticesInRange == 0)
				{
					InOutBatchHeader.IndexMin = VertexIndexRemap[InOutBatchHeader.IndexMin];
				}
				else if (NumRemovedVerticesInRange == BatchVertexRangeEnd - BatchVertexRangeBegin)
				{
					InOutBatchHeader.IndexMin    = 0;
					InOutBatchHeader.NumElements = 0;
				}
				else
				{
					TArrayView<uint32> BatchDataView = TArrayView<uint32>(
							DataView.GetData() + InOutBatchHeader.DataOffset/sizeof(uint32), 
							CalculateBatchDwords(InOutBatchHeader)); 

					TArrayView<FQuantizedDelta> BatchQuantizedDeltas(
							QuantizedDeltasStorage.GetData(), InOutBatchHeader.NumElements);

					ReadQuantizedDeltas(BatchQuantizedDeltas, InOutBatchHeader, BatchDataView); 
					
					const int32 NumDeltas = InOutBatchHeader.NumElements;
		
					int32 NumDeltaVerticesRemovedInRange = 0;
					for (int32 DeltaIndex = 0; DeltaIndex < NumDeltas; ++DeltaIndex)
					{
						NumDeltaVerticesRemovedInRange += static_cast<int32>(VertexIndexRemap[BatchQuantizedDeltas[DeltaIndex].Index] < 0);
					}

					if (NumDeltaVerticesRemovedInRange == 0)
					{
						InOutBatchHeader.IndexMin = (uint32)VertexIndexRemap[InOutBatchHeader.IndexMin];
					}
					else if (NumDeltaVerticesRemovedInRange == BatchVertexRangeEnd - BatchVertexRangeBegin)
					{
						InOutBatchHeader.IndexMin	= 0;
						InOutBatchHeader.NumElements = 0;
					}
					else
					{
						check(InOutBatchHeader.NumElements <= UE::MorphTargetVertexCodec::BatchSize);
						TArrayView<FQuantizedDelta> KeptQuantizedDeltas(
								QuantizedDeltasStorage.GetData() + UE::MorphTargetVertexCodec::BatchSize, 
								InOutBatchHeader.NumElements);

						int32 DeltaIndex = 0;
						for ( ; DeltaIndex < NumDeltas; ++DeltaIndex)
						{
							int32 RemappedDeltaVertexIdx = VertexIndexRemap[BatchQuantizedDeltas[DeltaIndex].Index];
							if (RemappedDeltaVertexIdx >= 0)
							{
								InOutBatchHeader.IndexMin = (uint32)RemappedDeltaVertexIdx;
								break;
							}
						}

						uint32 DestDeltaIndex = 0;
						for ( ; DeltaIndex < NumDeltas; ++DeltaIndex)
						{
							int32 RemappedDeltaVertexIdx = VertexIndexRemap[BatchQuantizedDeltas[DeltaIndex].Index];
							if (RemappedDeltaVertexIdx >= 0)
							{
								KeptQuantizedDeltas[DestDeltaIndex++] = FQuantizedDelta
								{
									.Position = BatchQuantizedDeltas[DeltaIndex].Position,
									.TangentZ = BatchQuantizedDeltas[DeltaIndex].TangentZ,
									.Index    = (uint32)RemappedDeltaVertexIdx,
								};	
							}
						}

						InOutBatchHeader.NumElements = DestDeltaIndex;
						WriteQuantizedDeltas(KeptQuantizedDeltas, InOutBatchHeader, BatchDataView);
					}
				}
			};

			const int32 NumBatches = Result.Morph.BatchesPerMorph[MorphIndex];
			
			if (NumBatches == 0)
			{
				continue;
			}

			int32 BatchStartOffset = Result.Morph.BatchStartOffsetPerMorph[MorphIndex];

			TArrayView<uint32> DataView = MakeArrayView(Result.MorphDataBuffer); 

			FDeltaBatchHeader BatchHeaderStorage[2];

			FDeltaBatchHeader* CurrBatchHeader = &BatchHeaderStorage[0];
			FDeltaBatchHeader* NextBatchHeader = &BatchHeaderStorage[1];
			
			uint32* MorphDataPtr = DataView.GetData();

			ReadHeader(*CurrBatchHeader, TArrayView<uint32>(MorphDataPtr + (BatchStartOffset + 0) * NumBatchHeaderDwords, NumBatchHeaderDwords));

			for (int32 BatchIndex = 0; BatchIndex < NumBatches - 1; ++BatchIndex)
			{
				ReadHeader(*NextBatchHeader, TArrayView<uint32>(MorphDataPtr + (BatchStartOffset + BatchIndex + 1) * NumBatchHeaderDwords, NumBatchHeaderDwords));
				
				ProcessBatchData(*CurrBatchHeader, DataView, CurrBatchHeader->IndexMin, NextBatchHeader->IndexMin);
				
				WriteHeader(*CurrBatchHeader, TArrayView<uint32>(MorphDataPtr + (BatchStartOffset + BatchIndex) * NumBatchHeaderDwords, NumBatchHeaderDwords));
				::Swap(CurrBatchHeader, NextBatchHeader);
			}

			const int32 NumVertices = VertexIndexRemap.Num();
			ProcessBatchData(*CurrBatchHeader, DataView, CurrBatchHeader->IndexMin, NumVertices);
			WriteHeader(*CurrBatchHeader, TArrayView<uint32>(MorphDataPtr + (BatchStartOffset + NumBatches - 1) * NumBatchHeaderDwords, NumBatchHeaderDwords));
		}
	}

    void MeshRemoveRecreateSurface(FMesh* Result, const TBitArray<>& UsedVertices, const TBitArray<>& UsedFaces)
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveRecreateSurface);
		TArray<FSurfaceSubMesh, TInlineAllocator<32>> OrigSubMeshes;
        for (FMeshSurface& ResultSurf : Result->Surfaces)
        {
			OrigSubMeshes = ResultSurf.SubMeshes;
			ResultSurf.SubMeshes.Reset();            

			int32 PrevMeshRangeVertexEnd = 0;
            int32 PrevMeshRangeIndexEnd = 0;
            for (const FSurfaceSubMesh& SubMesh : OrigSubMeshes)
            {
                int32 MeshRangeVertexEnd = PrevMeshRangeVertexEnd; 
                int32 MeshRangeIndexEnd = PrevMeshRangeIndexEnd;
               
                MeshRangeVertexEnd += UsedVertices.CountSetBits(SubMesh.VertexBegin, SubMesh.VertexEnd);
				
                // Only add the mesh if it has remaining vertices. 
                if (MeshRangeVertexEnd > PrevMeshRangeVertexEnd)
                {
					check(SubMesh.IndexBegin % 3 == 0);
					check((SubMesh.IndexEnd - SubMesh.IndexBegin) % 3 == 0);
                    MeshRangeIndexEnd += UsedFaces.CountSetBits(SubMesh.IndexBegin/3, SubMesh.IndexEnd/3)*3;

                    ResultSurf.SubMeshes.Emplace(FSurfaceSubMesh 
                            {
                                PrevMeshRangeVertexEnd, MeshRangeVertexEnd,
                                PrevMeshRangeIndexEnd, MeshRangeIndexEnd,
                                SubMesh.ExternalId 
                            });
                }

                PrevMeshRangeVertexEnd = MeshRangeVertexEnd;
                PrevMeshRangeIndexEnd = MeshRangeIndexEnd;
            }
        }
		
		// Remove Empty surfaces but always keep the first one. 
		// The previous step has eliminated empty submeshes, so it is only needed to check if the surface has 
		// any submesh.
		for (int32 I = Result->Surfaces.Num() - 1; I >= 1; --I)
		{
			if (!Result->Surfaces[I].SubMeshes.Num())
			{
				Result->Surfaces.RemoveAt(I, EAllowShrinking::No);
			}
		}

		check(Result->Surfaces.Num() >= 1);
	
		// Add a defaulted empty submesh if the surface is empty. A surface always needs a submesh 
		// even if empty.
		if (!Result->Surfaces[0].SubMeshes.Num())
		{
			Result->Surfaces[0].SubMeshes.Emplace();
		}
    }

    //---------------------------------------------------------------------------------------------
    struct FIdInterval
    {
        uint64 idStart;
        int32 idPosition;
        int32 size;
    };


    void ExtractVertexIndexIntervals( TArray<FIdInterval>& intervals, const FMesh* Source )
    {
		MeshVertexIdIteratorConst itVI(Source);
		FIdInterval current;
		current.idStart = FMesh::InvalidVertexId;
		current.idPosition = 0;
		current.size = 0;
		for ( int32 sv=0; sv< Source->GetVertexBuffers().GetElementCount(); ++sv )
        {
            uint64 id = itVI.Get();
            ++itVI;

            if (current.idStart== FMesh::InvalidVertexId)
            {
                current.idStart = id;
                current.idPosition = sv;
                current.size = 1;
            }
            else
            {
                if (id==current.idStart+current.size)
                {
                    ++current.size;
                }
                else
                {
                    intervals.Add(current);
                    current.idStart = id;
                    current.idPosition = sv;
                    current.size = 1;
                }
            }
        }

        if (current.idStart!= FMesh::InvalidVertexId)
        {
            intervals.Add(current);
        }
    }


    int64 FindPositionInIntervals( const TArray<FIdInterval>& intervals, int64 id )
    {
        for( const FIdInterval& interval: intervals )
        {
            int64 deltaId = id - interval.idStart;
            if (deltaId>=0 && deltaId<interval.size)
            {
                return interval.idPosition+deltaId;
            }
        }
        return -1;
    }

	
    void MeshRemoveVerticesWithCullSet(FMesh* Result, const TBitArray<>& VerticesToCull, bool bRemoveIfAllVerticesCulled)
    { 
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveVerticesWithCullSet);
		
        const UntypedMeshBufferIterator IndicesBegin(Result->GetIndexBuffers(), EMeshBufferSemantic::VertexIndex);
		
        const int32 NumFaces = Result->GetFaceCount();
        const int32 NumVertices = Result->GetVertexCount();

		const uint32 IndexTypeSize = IndicesBegin.GetElementSize();
		check(IndexTypeSize == 4 || IndexTypeSize == 2);

		static constexpr int32 MaxNumBatches = 5;
		TStaticArray<TBitArray<>, MaxNumBatches> ParallelUsedVertices;
		TStaticArray<TBitArray<>, MaxNumBatches> ParallelUsedFaces;

		const TBitArray<>& UsedVertices = ParallelUsedVertices[0];
		const TBitArray<>& UsedFaces = ParallelUsedFaces[0];
		
		ParallelUsedFaces[0].Reserve(NumFaces);

		{
			const bool bUseBatches = NumFaces > (1 << 13);
			const int32 NumBatches = bUseBatches ? MaxNumBatches : 1;
			const int32 NumFacesPerBatch = FMath::DivideAndRoundUp<int32>(NumFaces, NumBatches);

			ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
				[
					IndicesBegin,
					IndexTypeSize,
					&VerticesToCull,
					&ParallelUsedVertices,
					&ParallelUsedFaces,
					NumFaces,
					NumFacesPerBatch,
					NumVertices,
					bRemoveIfAllVerticesCulled
				](int32 BatchId)
			{
				const int32 BatchBeginFaceIndex = BatchId * NumFacesPerBatch;
				const int32 BatchEndFaceIndex = FMath::Min(BatchBeginFaceIndex + NumFacesPerBatch, NumFaces);
				const int32 BatchNumFaces = BatchEndFaceIndex - BatchBeginFaceIndex;

				TBitArray<>& UsedFaces = ParallelUsedFaces[BatchId];
				UsedFaces.SetNum(BatchNumFaces, false);

				TBitArray<>& UsedVertices = ParallelUsedVertices[BatchId];
				UsedVertices.SetNum(NumVertices, false);

				if (IndexTypeSize == 4)
				{
					const uint32* FaceIndicesData = reinterpret_cast<uint32*>((IndicesBegin + BatchBeginFaceIndex * 3).ptr());
					for (int32 FaceIndex = 0; FaceIndex < BatchNumFaces; ++FaceIndex)
					{
						bool bRemoved = false;

						if (bRemoveIfAllVerticesCulled)
						{
							const bool bAllVertsRemoved =
								VerticesToCull[FaceIndicesData[0]] &
								VerticesToCull[FaceIndicesData[1]] &
								VerticesToCull[FaceIndicesData[2]];
							bRemoved = bAllVertsRemoved;
						}
						else
						{
							const bool bOneVertRemoved =
								VerticesToCull[FaceIndicesData[0]] |
								VerticesToCull[FaceIndicesData[1]] |
								VerticesToCull[FaceIndicesData[2]];
							bRemoved = bOneVertRemoved;
						}

						if (!bRemoved)
						{
							UsedFaces[FaceIndex] = true;

							UsedVertices[FaceIndicesData[0]] = true;
							UsedVertices[FaceIndicesData[1]] = true;
							UsedVertices[FaceIndicesData[2]] = true;
						}

						FaceIndicesData += 3;
					}
				}
				else if (IndexTypeSize == 2)
				{
					const uint16* FaceIndicesData = reinterpret_cast<uint16*>((IndicesBegin + BatchBeginFaceIndex * 3).ptr());

					for (int32 FaceIndex = 0; FaceIndex < BatchNumFaces; ++FaceIndex)
					{
						bool bRemoved = false;

						if (bRemoveIfAllVerticesCulled)
						{
							const bool bAllVertsRemoved =
								VerticesToCull[FaceIndicesData[0]] &
								VerticesToCull[FaceIndicesData[1]] &
								VerticesToCull[FaceIndicesData[2]];
							bRemoved = bAllVertsRemoved;
						}
						else
						{
							const bool bOneVertRemoved =
								VerticesToCull[FaceIndicesData[0]] |
								VerticesToCull[FaceIndicesData[1]] |
								VerticesToCull[FaceIndicesData[2]];
							bRemoved = bOneVertRemoved;
						}

						if (!bRemoved)
						{
							UsedFaces[FaceIndex] = true;

							UsedVertices[FaceIndicesData[0]] = true;
							UsedVertices[FaceIndicesData[1]] = true;
							UsedVertices[FaceIndicesData[2]] = true;
						}

						FaceIndicesData += 3;
					}
				}
			});

			for (int32 Index = 1; Index < NumBatches; ++Index)
			{
				ParallelUsedFaces[0].AddRange(ParallelUsedFaces[Index], ParallelUsedFaces[Index].Num(), 0);
				ParallelUsedVertices[0].CombineWithBitwiseOR(ParallelUsedVertices[Index], EBitwiseOperatorFlags::MaxSize);
			}
		}

		const bool bHasFaces = UsedFaces.Find(true) != INDEX_NONE;
		const bool bHasRemovedFaces = UsedFaces.Find(false) != INDEX_NONE;

		if (bHasFaces && bHasRemovedFaces && Result->AreVertexIdsImplicit())
		{
			Result->MakeVertexIdsRelative();
		}

		TArray<int32> UsedVerticesMap;
#if DO_CHECK
        UsedVerticesMap.Init(-1, NumVertices);
#else
        // This data will only be accessed by indices that have been mapped, No need to initialize. 
		UsedVerticesMap.SetNumUninitialized(NumVertices);
#endif
		FMeshBufferSet& VertexBufferSet = Result->GetVertexBuffers();

		const int32 NumBuffers = VertexBufferSet.GetBufferCount();

		// Compute vertices indices remap
        int32 NumVerticesRemaining = 0;
		if (bHasFaces)
        {
		    int32 LastFreeVertexIndex = 0;
            for (int32 VertexIndex = UsedVertices.FindFrom(true, 0); VertexIndex >= 0;)
			{
                const int32 UsedSpanBegin = VertexIndex;
                VertexIndex = UsedVertices.FindFrom(false, VertexIndex);
				
				// At the end of the buffer we may not find a false element, in that case
				// FindForm returns INDEX_NONE, set the vertex at the range end. 
                VertexIndex = VertexIndex >= 0 ? VertexIndex : NumVertices;
                const int32 UsedSpanEnd = VertexIndex;

				// VertexIndex may be one past the end of the array, VertexIndex will become INDEX_NONE
				// and the loop will finish.
                VertexIndex = UsedVertices.FindFrom(true, VertexIndex);
				
                for (int32 I = UsedSpanBegin; I < UsedSpanEnd; ++I)
                {
                    UsedVerticesMap[I] = LastFreeVertexIndex + I - UsedSpanBegin;
                }

				LastFreeVertexIndex += UsedSpanEnd - UsedSpanBegin;
			}

            NumVerticesRemaining = LastFreeVertexIndex;
		}

		// Copy move buffers. We are recomputing the spans for each buffer, should be ok as
        // finding the span is fast compared to the data move.
        if (NumVerticesRemaining > 0)
        {
            for (int32 BufferIndex = 0; BufferIndex < NumBuffers; ++BufferIndex)
            {
                uint8* BufferData = VertexBufferSet.GetBufferData(BufferIndex); 
                const uint32 ElemSize = VertexBufferSet.GetElementSize(BufferIndex);

                int32 LastFreeVertexIndex = 0;

                for (int32 VertexIndex = UsedVertices.FindFrom(true, 0); VertexIndex >= 0;)
                {
                    const int32 UsedSpanBegin = VertexIndex;
                    VertexIndex = UsedVertices.FindFrom(false, VertexIndex);
                    VertexIndex = VertexIndex >= 0 ? VertexIndex : NumVertices;
                    const int32 UsedSpanEnd = VertexIndex;

                    VertexIndex = UsedVertices.FindFrom(true, VertexIndex);

                    // Copy vertex buffer span.	
                    const int32 UsedSpanSize = UsedSpanEnd - UsedSpanBegin;
                    
                    if (LastFreeVertexIndex != UsedSpanBegin)
                    {
                        FMemory::Memmove(
                                BufferData + LastFreeVertexIndex*ElemSize,
                                BufferData + UsedSpanBegin*ElemSize,
                                UsedSpanSize*ElemSize);
                    }

                    LastFreeVertexIndex += UsedSpanSize;
                }

                check(LastFreeVertexIndex == NumVerticesRemaining);
            }
        }
		Result->GetVertexBuffers().SetElementCount(NumVerticesRemaining);

		int32 LastFreeFaceIndex = 0;
		if (bHasFaces)
		{
			// Move Indices
			for (int32 FaceIndex = UsedFaces.FindFrom(true, 0); FaceIndex >= 0;)
			{
				const int32 UsedSpanStart = FaceIndex;
				FaceIndex = UsedFaces.FindFrom(false, FaceIndex);
				FaceIndex = FaceIndex >= 0 ? FaceIndex : NumFaces;
				const int32 UsedSpanEnd = FaceIndex;

				FaceIndex = UsedFaces.FindFrom(true, FaceIndex);

				const int32 UsedSpanSize = UsedSpanEnd - UsedSpanStart;
				check(UsedSpanSize > 0);

				if (LastFreeFaceIndex != UsedSpanStart)
				{
					FMemory::Memmove(
						IndicesBegin.ptr() + LastFreeFaceIndex * IndexTypeSize * 3,
						IndicesBegin.ptr() + UsedSpanStart * IndexTypeSize * 3,
						UsedSpanSize * IndexTypeSize * 3);
				}

				LastFreeFaceIndex += UsedSpanSize;
			}

			constexpr int32 NumFacesPerBatch = (1 << 13);
			const int32 NumBatches = FMath::DivideAndRoundUp<int32>(LastFreeFaceIndex, NumFacesPerBatch);

			// Remap Vertices
			ParallelExecutionUtils::InvokeBatchParallelFor(NumBatches,
				[
					IndicesBegin,
					IndexTypeSize,
					&UsedVerticesMap,
					LastFreeFaceIndex,
					NumFacesPerBatch
				](int32 BatchId)
				{
					const int32 BatchBeginFaceIndex = BatchId * NumFacesPerBatch;
					const int32 BatchEndFaceIndex = FMath::Min(BatchBeginFaceIndex + NumFacesPerBatch, LastFreeFaceIndex);

					if (IndexTypeSize == 4)
					{
						uint32* FaceIndicesData = reinterpret_cast<uint32*>((IndicesBegin + BatchBeginFaceIndex * 3).ptr());
						for (int32 FaceIndex = BatchBeginFaceIndex; FaceIndex < BatchEndFaceIndex; ++FaceIndex)
						{
							check(UsedVerticesMap[FaceIndicesData[0]] >= 0);
							check(UsedVerticesMap[FaceIndicesData[1]] >= 0);
							check(UsedVerticesMap[FaceIndicesData[2]] >= 0);

							FaceIndicesData[0] = UsedVerticesMap[FaceIndicesData[0]];
							FaceIndicesData[1] = UsedVerticesMap[FaceIndicesData[1]];
							FaceIndicesData[2] = UsedVerticesMap[FaceIndicesData[2]];

							FaceIndicesData += 3;
						}
					}
					else if (IndexTypeSize == 2)
					{
						uint16* FaceIndicesData = reinterpret_cast<uint16*>((IndicesBegin + BatchBeginFaceIndex).ptr());
						for (int32 FaceIndex = BatchBeginFaceIndex; FaceIndex < BatchEndFaceIndex; ++FaceIndex)
						{
							check(UsedVerticesMap[FaceIndicesData[0]] >= 0);
							check(UsedVerticesMap[FaceIndicesData[1]] >= 0);
							check(UsedVerticesMap[FaceIndicesData[2]] >= 0);

							FaceIndicesData[0] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[0]]);
							FaceIndicesData[1] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[1]]);
							FaceIndicesData[2] = static_cast<uint16>(UsedVerticesMap[FaceIndicesData[2]]);

							FaceIndicesData += 3;
						}
					}
				});
		}

		check(LastFreeFaceIndex <= NumFaces);

		Result->GetIndexBuffers().SetElementCount(LastFreeFaceIndex*3);

		MeshRemoveRecreateSurface(Result, UsedVertices, UsedFaces);

		MeshRemoveMeshMorphs(*Result, UsedVerticesMap);

    }

	void MeshRemoveMaskInline(FMesh* Mesh, const FMesh* Mask, bool bRemoveIfAllVerticesCulled)
    {
        MUTABLE_CPUPROFILER_SCOPE(MeshRemoveMask);

        if (!Mask->GetVertexCount() || !Mesh->GetVertexCount() || !Mesh->GetIndexCount())
        {
            return;
        }

		int32 MaskElementCount = Mask->GetVertexBuffers().GetElementCount();

        // For each source vertex, true if it is removed.
        int32 MeshVertexCount = Mesh->GetVertexCount();
		TBitArray<> RemovedVertices;
		RemovedVertices.SetNum(MeshVertexCount, false);
        {
			TArray<FIdInterval> Intervals;
            ExtractVertexIndexIntervals(Intervals, Mesh);

			MeshVertexIdIteratorConst itMaskVI(Mask);
			for ( int32 mv=0; mv<MaskElementCount; ++mv )
            {
                uint64 MaskVertexId = itMaskVI.Get();
                ++itMaskVI;

                int32 IndexInSource = FindPositionInIntervals(Intervals, MaskVertexId);
                if (IndexInSource >= 0)
                {
					RemovedVertices[IndexInSource] = true;
                }
            }
        }
		
        MeshRemoveVerticesWithCullSet(Mesh, RemovedVertices, bRemoveIfAllVerticesCulled);
	}
}
