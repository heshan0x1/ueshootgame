// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpMeshMorph.h"

#include "MuR/MeshPrivate.h"
#include "MuR/ConvertData.h"
#include "MuR/MutableTrace.h"
#include "MuR/SparseIndexMap.h"
#include "PackedNormal.h"
#include "MuR/Mesh.h"
#include "UObject/NameTypes.h"
#include "Animation/MorphTarget.h"
#include "Rendering/MorphTargetVertexCodec.h"

void UE::Mutable::Private::MeshMorph(FMesh* Mesh, const FName& MorphName, const float Factor)
{
	MUTABLE_CPUPROFILER_SCOPE(MeshMorph_Parameter);

	using namespace UE::MorphTargetVertexCodec;

	if (!Mesh)
	{
		return;
	}

	const int32 MorphIndex = Mesh->Morph.Names.Find(MorphName);
	if (MorphIndex == INDEX_NONE)
	{
		return;
	}
	
	// Get pointers to vertex position data
	MeshBufferIterator<EMeshBufferFormat::Float32, float, 3> MeshPositionIter(Mesh->VertexBuffers, EMeshBufferSemantic::Position, 0);	
	const bool bHasPositions = MeshPositionIter.ptr() != nullptr;
	
	// {BiNormal, Tangent, Normal}
	TStaticArray<UntypedMeshBufferIterator, 3> MeshTangentFrameChannelsIters;
	
	const FMeshBufferSet& VertexBufferSet = Mesh->GetVertexBuffers();

	const uint32 NumVertices = VertexBufferSet.GetElementCount();

	int32 NormalBufferIndex   = -1;
	int32 NormalBufferChannel = -1;
	
	VertexBufferSet.FindChannel(EMeshBufferSemantic::Normal, 0, &NormalBufferIndex, &NormalBufferChannel);
	int32 NormalNumChannels = NormalBufferIndex >= 0 ? VertexBufferSet.Buffers[NormalBufferIndex].Channels.Num() : 0;
	
	const bool bHasNormals = NormalBufferIndex >= 0;

	for (int32 ChannelIndex = 0; ChannelIndex < NormalNumChannels; ++ChannelIndex)
	{
		const FMeshBufferChannel& Channel = VertexBufferSet.Buffers[NormalBufferIndex].Channels[ChannelIndex];

		EMeshBufferSemantic Sem = Channel.Semantic;
		int32 SemIndex = Channel.SemanticIndex;

		if (Sem == EMeshBufferSemantic::Normal && bHasNormals)
		{
			MeshTangentFrameChannelsIters[2] = UntypedMeshBufferIterator(Mesh->GetVertexBuffers(), Sem, SemIndex);
		}
		else if (Sem == EMeshBufferSemantic::Tangent && bHasNormals)
		{
			MeshTangentFrameChannelsIters[1] = UntypedMeshBufferIterator(Mesh->GetVertexBuffers(), Sem, SemIndex);
		}
		else if (Sem == EMeshBufferSemantic::Binormal && bHasNormals)
		{
			MeshTangentFrameChannelsIters[0] = UntypedMeshBufferIterator(Mesh->GetVertexBuffers(), Sem, SemIndex);
		}
	}

	const UntypedMeshBufferIterator& MeshNormalIter = MeshTangentFrameChannelsIters[2];
	const UntypedMeshBufferIterator& MeshTangentIter = MeshTangentFrameChannelsIters[1];
	const UntypedMeshBufferIterator& MeshBiNormalIter = MeshTangentFrameChannelsIters[0];

	const EMeshBufferFormat NormalFormat = MeshNormalIter.GetFormat();
	const int32 NormalComps = MeshNormalIter.GetComponents();

	const EMeshBufferFormat TangentFormat = MeshTangentIter.GetFormat();
	const int32 TangentComps = MeshTangentIter.GetComponents();

	const EMeshBufferFormat BiNormalFormat = MeshBiNormalIter.GetFormat();
	const int32 BiNormalComps = MeshBiNormalIter.GetComponents();
	
	const bool bHasOptimizedNormals = NormalFormat == EMeshBufferFormat::PackedDirS8_W_TangentSign
		&& (!MeshTangentIter.ptr() || TangentFormat == EMeshBufferFormat::PackedDirS8) && !MeshBiNormalIter.ptr();
	
	const uint32 BatchStartOffset = Mesh->Morph.BatchStartOffsetPerMorph[MorphIndex];
	const uint32 MorphNumBatches  = Mesh->Morph.BatchesPerMorph[MorphIndex];
	for (uint32 BatchHeaderIndex = 0; BatchHeaderIndex < MorphNumBatches; ++BatchHeaderIndex)
	{
		TArrayView<uint32> BatchHeaderData(
				&Mesh->MorphDataBuffer[(BatchStartOffset + BatchHeaderIndex) * NumBatchHeaderDwords], 
				NumBatchHeaderDwords);

		FDeltaBatchHeader BatchHeader;
		ReadHeader(BatchHeader, BatchHeaderData);

		if (BatchHeader.NumElements == 0)
		{
			continue;
		}

		TArray<FQuantizedDelta, TInlineAllocator<UE::MorphTargetVertexCodec::BatchSize>> QuantizedDeltas;
		QuantizedDeltas.SetNumUninitialized(BatchHeader.NumElements);

		TArrayView<uint32> Data(
				&Mesh->MorphDataBuffer[BatchHeader.DataOffset / sizeof(uint32)], 
				CalculateBatchDwords(BatchHeader));

		ReadQuantizedDeltas(QuantizedDeltas, BatchHeader, Data);

		for (FQuantizedDelta& QuantizedDelta : QuantizedDeltas)
		{
			FMorphTargetDelta Delta;
			DequantizeDelta(Delta, BatchHeader.bTangents, QuantizedDelta, Mesh->Morph.PositionPrecision, Mesh->Morph.TangentZPrecision);

			// Positions
			if (bHasPositions)
			{
				check(Delta.SourceIdx < NumVertices)
				FVector3f& Position = *reinterpret_cast<FVector3f*>(*(MeshPositionIter + Delta.SourceIdx));
				Position += Delta.PositionDelta * Factor;
			}

			// Normals
			if (bHasOptimizedNormals)
			{
				// Normal
				check(Delta.SourceIdx < NumVertices)
				FPackedNormal* PackedNormal = reinterpret_cast<FPackedNormal*>((MeshNormalIter + Delta.SourceIdx).ptr());
				int8 W = PackedNormal->Vector.W;
				const FVector3f BaseNormal = PackedNormal->ToFVector3f();
				
				const FVector3f Normal = (BaseNormal + Delta.TangentZDelta * Factor).GetSafeNormal();

				*PackedNormal = Normal;
				PackedNormal->Vector.W = W;

				// Tangent
				if (MeshTangentIter.ptr())
				{
					check(Delta.SourceIdx < NumVertices)
					FPackedNormal* PackedTangent = reinterpret_cast<FPackedNormal*>((MeshTangentIter + Delta.SourceIdx).ptr());
					const FVector3f BaseTangent = PackedTangent->ToFVector3f();

					// Orthogonalize Tangent based on new Normal. This assumes Normal and BaseTangent are normalized and different.
					const FVector3f Tangent = (BaseTangent - FVector3f::DotProduct(Normal, BaseTangent) * Normal).GetSafeNormal();

					*PackedTangent = Tangent;
				}
			}
			else if (MeshNormalIter.ptr())
			{
				// When normal is packed, binormal channel is not expected. It is not a big deal if it's there but we would be doing extra unused work in that case. 
				ensure(!(NormalFormat == EMeshBufferFormat::PackedDir8_W_TangentSign || NormalFormat == EMeshBufferFormat::PackedDirS8_W_TangentSign) || !MeshBiNormalIter.ptr());
				
				MUTABLE_CPUPROFILER_SCOPE(ApplyNormalMorph_SlowPath);
				
				UntypedMeshBufferIterator NormalIter = MeshNormalIter + Delta.SourceIdx;

				const FVector3f BaseNormal = NormalIter.GetAsVec3f();

				const FVector3f Normal = (BaseNormal + Delta.TangentZDelta * Factor).GetSafeNormal();

				// Leave the tangent basis sign untouched for packed normals formats.
				for (int32 C = 0; C < NormalComps && C < 3; ++C)
				{
					ConvertData(C, NormalIter.ptr(), NormalFormat, &Normal, EMeshBufferFormat::Float32);
				}

				// Tangent
				if (MeshTangentIter.ptr())
				{
					UntypedMeshBufferIterator TangentIter = MeshTangentIter + Delta.SourceIdx;

					const FVector3f BaseTangent = TangentIter.GetAsVec3f();

					// Orthogonalize Tangent based on new Normal. This assumes Normal and BaseTangent are normalized and different.
					const FVector3f Tangent = (BaseTangent - FVector3f::DotProduct(Normal, BaseTangent) * Normal).GetSafeNormal();

					for (int32 C = 0; C < TangentComps && C < 3; ++C)
					{
						ConvertData(C, TangentIter.ptr(), TangentFormat, &Tangent, EMeshBufferFormat::Float32);
					}

					// BiNormal
					if (MeshBiNormalIter.ptr())
					{
						UntypedMeshBufferIterator BiNormalIter = MeshBiNormalIter + Delta.SourceIdx;

						const FVector3f& N = BaseNormal;
						const FVector3f& T = BaseTangent;
						const FVector3f  B = BiNormalIter.GetAsVec3f();

						const float BaseTangentBasisDeterminant =
							B.X * T.Y * N.Z + B.Z * T.X * N.Y + B.Y * T.Z * N.Y -
							B.Z * T.Y * N.X - B.Y * T.X * N.Z - B.X * T.Z * N.Y;

						const float BaseTangentBasisDeterminantSign = BaseTangentBasisDeterminant >= 0 ? 1.0f : -1.0f;

						const FVector3f BiNormal = FVector3f::CrossProduct(Tangent, Normal) * BaseTangentBasisDeterminantSign;

						for (int32 C = 0; C < BiNormalComps && C < 3; ++C)
						{
							ConvertData(C, BiNormalIter.ptr(), BiNormalFormat, &BiNormal, EMeshBufferFormat::Float32);
						}
					}
				}
			}
		}
	}
}

void UE::Mutable::Private::MeshMorph(FMesh* BaseMesh, const FMesh* MaxMesh, const float Factor)
{
    MUTABLE_CPUPROFILER_SCOPE(MeshMorph_Compiled);

	if (!BaseMesh)
	{
		return;
	}

	const auto MakeIndexMap = [](
		MeshVertexIdIteratorConst BaseIdIter, int32 BaseNum,
		MeshVertexIdIteratorConst MorphIdIter, int32 MorphNum)
	-> SparseIndexMapSet
	{	
		MUTABLE_CPUPROFILER_SCOPE(MakeIndexMap);
		TArray<SparseIndexMapSet::FRangeDesc> RangeDescs;

		// Detect all ranges and their limits
		{
			for (int32 Index = 0; Index < BaseNum; ++Index, ++BaseIdIter)
			{
				const uint64 BaseId = BaseIdIter.Get();

				uint32 Prefix = BaseId >> 32;
				uint32 Id = BaseId & 0xffffffff;
				bool bFound = false;
				for (SparseIndexMapSet::FRangeDesc& Range : RangeDescs)
				{
					if (Range.Prefix == Prefix)
					{
						Range.MinIndex = FMath::Min(Id, Range.MinIndex);
						Range.MaxIndex = FMath::Max(Id, Range.MaxIndex);
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					RangeDescs.Add({Prefix, Id, Id});
				}
			}
		}

		SparseIndexMapSet IndexMap(RangeDescs);
       
        for (int32 Index = 0; Index < MorphNum; ++Index, ++MorphIdIter)
        {
            const uint64 MorphId = MorphIdIter.Get();
            
            IndexMap.Insert(MorphId, Index);
        }

		return IndexMap;
	};

	// Number of vertices to modify
	const int32 MaxNum = MaxMesh ? MaxMesh->GetVertexBuffers().GetElementCount() : 0;
	const int32 BaseNum = BaseMesh ? BaseMesh->GetVertexBuffers().GetElementCount() : 0;
	const FMesh* RefTarget = MaxMesh;

	if (BaseNum == 0 || MaxNum == 0)
	{
		return;
	}

	if (!RefTarget)
	{
		return;
	}
	
	constexpr int32 MorphBufferDataChannel = 0;
	const int32 ChannelsNum = RefTarget->GetVertexBuffers().GetBufferChannelCount(MorphBufferDataChannel);

	TArray<UntypedMeshBufferIterator> BaseGenericIters;
	BaseGenericIters.SetNum(ChannelsNum);
	TArray<UntypedMeshBufferIteratorConst> MinGenericIters;
	MinGenericIters.SetNum(ChannelsNum);
	TArray<UntypedMeshBufferIteratorConst> MaxGenericIters;
	MaxGenericIters.SetNum(ChannelsNum);

	// Get pointers to vertex position data
	MeshBufferIterator<EMeshBufferFormat::Float32, float, 3> BasePositionIter(BaseMesh->VertexBuffers, EMeshBufferSemantic::Position, 0);	
	MeshBufferIteratorConst<EMeshBufferFormat::Float32, float, 3> MaxPositionIter(RefTarget->VertexBuffers, EMeshBufferSemantic::Position, 0);

	// {BiNormal, Tangent, Normal}
	TStaticArray<UntypedMeshBufferIterator, 3> BaseTangentFrameChannelsIters;
	UntypedMeshBufferIteratorConst MaxNormalChannelIter;

	const bool bBaseHasNormals = UntypedMeshBufferIteratorConst(BaseMesh->GetVertexBuffers(), EMeshBufferSemantic::Normal, 0).ptr() != nullptr;
	for (int32 ChannelIndex = 0; ChannelIndex < ChannelsNum; ++ChannelIndex)
	{
		const FMeshBufferSet& MBSPriv = RefTarget->GetVertexBuffers();
		const FMeshBufferChannel& Channel = MBSPriv.Buffers[MorphBufferDataChannel].Channels[ChannelIndex];
		EMeshBufferSemantic Sem = Channel.Semantic;
		int32 SemIndex = Channel.SemanticIndex;
		if (Sem == EMeshBufferSemantic::Normal && bBaseHasNormals)
		{
			BaseTangentFrameChannelsIters[2] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
			MaxNormalChannelIter = UntypedMeshBufferIteratorConst(MaxMesh->GetVertexBuffers(), Sem, SemIndex);
		}
		else if (Sem == EMeshBufferSemantic::Tangent && bBaseHasNormals)
		{
			BaseTangentFrameChannelsIters[1] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
		}
		else if (Sem == EMeshBufferSemantic::Binormal && bBaseHasNormals)
		{
			BaseTangentFrameChannelsIters[0] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
		}
		else if(Sem != EMeshBufferSemantic::Position)
		{
			BaseGenericIters[ChannelIndex] = UntypedMeshBufferIterator(BaseMesh->GetVertexBuffers(), Sem, SemIndex);
			MaxGenericIters[ChannelIndex] = UntypedMeshBufferIteratorConst(MaxMesh->GetVertexBuffers(), Sem, SemIndex);
		}
	}

	MeshVertexIdIteratorConst BaseIdIter(BaseMesh);
	
	MeshVertexIdIteratorConst MaxIdIter(MaxMesh);
	SparseIndexMapSet IndexMap = MakeIndexMap(BaseIdIter, BaseNum, MaxIdIter, MaxNum);
    
	MUTABLE_CPUPROFILER_SCOPE(ApplyMorph);

	const UntypedMeshBufferIterator& BaseNormalIter = BaseTangentFrameChannelsIters[2];
	const UntypedMeshBufferIterator& BaseTangentIter = BaseTangentFrameChannelsIters[1];
	const UntypedMeshBufferIterator& BaseBiNormalIter = BaseTangentFrameChannelsIters[0];

	const EMeshBufferFormat NormalFormat = BaseNormalIter.GetFormat();
	const int32 NormalComps = BaseNormalIter.GetComponents();

	const EMeshBufferFormat TangentFormat = BaseTangentIter.GetFormat();
	const int32 TangentComps = BaseTangentIter.GetComponents();

	const EMeshBufferFormat BiNormalFormat = BaseBiNormalIter.GetFormat();
	const int32 BiNormalComps = BaseBiNormalIter.GetComponents();

	const EMeshBufferFormat MorphNormalFormat = MaxNormalChannelIter.GetFormat();

	const bool bHasPositions = BasePositionIter.ptr() && MaxPositionIter.ptr();
	check((BasePositionIter.GetFormat() == EMeshBufferFormat::Float32 && BasePositionIter.GetComponents() == 3) || !bHasPositions);

	const bool bHasOptimizedNormals = NormalFormat == EMeshBufferFormat::PackedDirS8_W_TangentSign && MorphNormalFormat == EMeshBufferFormat::Float32
		&& (!BaseTangentIter.ptr() || TangentFormat == EMeshBufferFormat::PackedDirS8) && !BaseBiNormalIter.ptr();
	
	bool bHasGenericMorphs = false;
	const int32 ChannelNum = MaxGenericIters.Num();
	for (int32 ChannelIndex = 0; ChannelIndex < ChannelNum; ++ChannelIndex)
	{
		if (!(BaseGenericIters[ChannelIndex].ptr() && MaxGenericIters[ChannelIndex].ptr()))
		{
			continue;
		}

		bHasGenericMorphs = true;
	}

	for (int32 VertexIndex = 0; VertexIndex < BaseNum; ++VertexIndex)
	{
		const uint64 BaseId = (BaseIdIter + VertexIndex).Get();
		const uint32 MorphIndex = IndexMap.Find(BaseId);

		if (MorphIndex == SparseIndexMap::NotFoundValue)
		{
			continue;
		}

		// Find consecutive run.
		MeshVertexIdIteratorConst RunBaseIter = BaseIdIter + VertexIndex;
		MeshVertexIdIteratorConst RunMorphIter = MaxIdIter + MorphIndex;

		int32 RunSize = 0;
		for (; VertexIndex + RunSize < BaseNum && int32(MorphIndex) + RunSize < MaxNum && RunBaseIter.Get() == RunMorphIter.Get();
			++RunSize, ++RunBaseIter, ++RunMorphIter);

		// Positions
		if (bHasPositions)
		{
			for (int32 RunIndex = 0; RunIndex < RunSize; ++RunIndex)
			{
				FVector3f& Position = *reinterpret_cast<FVector3f*>(*(BasePositionIter + VertexIndex + RunIndex));
				const FVector3f& MorphPosition = *reinterpret_cast<const FVector3f*>(*(MaxPositionIter + MorphIndex + RunIndex));
				Position += MorphPosition * Factor;
			}
		}

		// Normals
		if (bHasOptimizedNormals)
		{
			for (int32 RunIndex = 0; RunIndex < RunSize; ++RunIndex)
			{
				// Normal
				FPackedNormal* PackedNormal = reinterpret_cast<FPackedNormal*>((BaseNormalIter + VertexIndex + RunIndex).ptr());
				int8 W = PackedNormal->Vector.W;
				const FVector3f BaseNormal = PackedNormal->ToFVector3f();

				const FVector3f* MorphNormal = reinterpret_cast<const FVector3f*>((MaxNormalChannelIter + MorphIndex + RunIndex).ptr());

				const FVector3f Normal = (BaseNormal + *MorphNormal * Factor).GetSafeNormal();

				*PackedNormal = *reinterpret_cast<const FVector3f*>(&Normal);
				PackedNormal->Vector.W = W;

				// Tangent
				if (BaseTangentIter.ptr())
				{
					FPackedNormal* PackedTangent = reinterpret_cast<FPackedNormal*>((BaseTangentIter + (VertexIndex + RunIndex)).ptr());
					const FVector3f BaseTangent = PackedTangent->ToFVector3f();

					// Orthogonalize Tangent based on new Normal. This assumes Normal and BaseTangent are normalized and different.
					const FVector3f Tangent = (BaseTangent - FVector3f::DotProduct(Normal, BaseTangent) * Normal).GetSafeNormal();

					*PackedTangent = *reinterpret_cast<const FVector3f*>(&Tangent);
				}
			}
		}
		else if (BaseNormalIter.ptr())
		{
			// When normal is packed, binormal channel is not expected. It is not a big deal if it's there but we would be doing extra unused work in that case. 
			ensure(!(NormalFormat == EMeshBufferFormat::PackedDir8_W_TangentSign || NormalFormat == EMeshBufferFormat::PackedDirS8_W_TangentSign) || !BaseBiNormalIter.ptr());
			
			MUTABLE_CPUPROFILER_SCOPE(ApplyNormalMorph_SlowPath);

			for (int32 RunIndex = 0; RunIndex < RunSize; ++RunIndex)
			{
				UntypedMeshBufferIterator NormalIter = BaseNormalIter + (VertexIndex + RunIndex);

				const FVector3f BaseNormal = NormalIter.GetAsVec3f();
				const FVector3f MorphNormal = (MaxNormalChannelIter + (MorphIndex + RunIndex)).GetAsVec3f();

				const FVector3f Normal = (BaseNormal + MorphNormal * Factor).GetSafeNormal();

				// Leave the tangent basis sign untouched for packed normals formats.
				for (int32 C = 0; C < NormalComps && C < 3; ++C)
				{
					ConvertData(C, NormalIter.ptr(), NormalFormat, &Normal, EMeshBufferFormat::Float32);
				}

				// Tangent
				if (BaseTangentIter.ptr())
				{
					UntypedMeshBufferIterator TangentIter = BaseTangentIter + (VertexIndex + RunIndex);

					const FVector3f BaseTangent = TangentIter.GetAsVec3f();

					// Orthogonalize Tangent based on new Normal. This assumes Normal and BaseTangent are normalized and different.
					const FVector3f Tangent = (BaseTangent - FVector3f::DotProduct(Normal, BaseTangent) * Normal).GetSafeNormal();

					for (int32 C = 0; C < TangentComps && C < 3; ++C)
					{
						ConvertData(C, TangentIter.ptr(), TangentFormat, &Tangent, EMeshBufferFormat::Float32);
					}

					// BiNormal
					if (BaseBiNormalIter.ptr())
					{
						UntypedMeshBufferIterator BiNormalIter = BaseBiNormalIter + (VertexIndex + RunIndex);

						const FVector3f& N = BaseNormal;
						const FVector3f& T = BaseTangent;
						const FVector3f  B = BiNormalIter.GetAsVec3f();

						const float BaseTangentBasisDeterminant =
							B.X * T.Y * N.Z + B.Z * T.X * N.Y + B.Y * T.Z * N.Y -
							B.Z * T.Y * N.X - B.Y * T.X * N.Z - B.X * T.Z * N.Y;

						const float BaseTangentBasisDeterminantSign = BaseTangentBasisDeterminant >= 0 ? 1.0f : -1.0f;

						const FVector3f BiNormal = FVector3f::CrossProduct(Tangent, Normal) * BaseTangentBasisDeterminantSign;

						for (int32 C = 0; C < BiNormalComps && C < 3; ++C)
						{
							ConvertData(C, BiNormalIter.ptr(), BiNormalFormat, &BiNormal, EMeshBufferFormat::Float32);
						}
					}
				}
			}
		}
		
		// Generic Morphs
		if (bHasGenericMorphs)
		{
			MUTABLE_CPUPROFILER_SCOPE(ApplyNormalMorph_Generic);
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelNum; ++ChannelIndex)
			{
				if (!(BaseGenericIters[ChannelIndex].ptr() && MaxGenericIters[ChannelIndex].ptr()))
				{
					continue;
				}

				UntypedMeshBufferIterator ChannelBaseIter = BaseGenericIters[ChannelIndex] + VertexIndex;
				UntypedMeshBufferIteratorConst ChannelMorphIter = MaxGenericIters[ChannelIndex] + MorphIndex;

				const EMeshBufferFormat DestChannelFormat = BaseGenericIters[ChannelIndex].GetFormat();
				const int32 DestChannelComps = BaseGenericIters[ChannelIndex].GetComponents();

				// Apply Morph to range found above.
				for (int32 R = 0; R < RunSize; ++R, ++ChannelBaseIter, ++ChannelMorphIter)
				{
					const FVector4f Value = ChannelBaseIter.GetAsVec4f() + ChannelMorphIter.GetAsVec4f() * Factor;

					// TODO: Optimize this for the specific components.
					// Max 4 components
					for (int32 Comp = 0; Comp < DestChannelComps && Comp < 4; ++Comp)
					{
						ConvertData(Comp, ChannelBaseIter.ptr(), DestChannelFormat, &Value, EMeshBufferFormat::Float32);
					}
				}
			}
		}

		VertexIndex += FMath::Max(RunSize - 1, 0);
	}
}
