// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteOrderedScatterUpdater.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ShaderCompilerCore.h"
#include "ShaderPermutationUtils.h"

namespace Nanite
{

class FScatterUpdates_CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScatterUpdates_CS);
	SHADER_USE_PARAMETER_STRUCT(FScatterUpdates_CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumUpdates)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, PackedUpdates)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, DstBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FScatterUpdates_CS, "/Engine/Private/Nanite/NaniteScatterUpdates.usf", "ScatterUpdates", SF_Compute);

static void AddPass_ScatterUpdates(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef DstBufferUAV, const TConstArrayView<FUintVector2> PackedUpdates)
{
	const uint32 NumUpdates = PackedUpdates.Num();
	if (NumUpdates == 0u)
	{
		return;
	}

	const uint32 NumUpdatesBufferElements = FMath::RoundUpToPowerOfTwo(NumUpdates);

	FRDGBufferRef UpdatesBuffer = CreateStructuredBuffer(	GraphBuilder, TEXT("Nanite.PackedScatterUpdatesBuffer"), PackedUpdates.GetTypeSize(),
															NumUpdatesBufferElements, PackedUpdates.GetData(), PackedUpdates.NumBytes());

	FScatterUpdates_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScatterUpdates_CS::FParameters>();
	PassParameters->NumUpdates		= NumUpdates;
	PassParameters->PackedUpdates	= GraphBuilder.CreateSRV(UpdatesBuffer);
	PassParameters->DstBuffer		= DstBufferUAV;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FScatterUpdates_CS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ScatterUpdates"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCountWrapped(NumUpdates, 64)
	);
}

FOrderedScatterUpdater::FOrderedScatterUpdater(uint32 NumExpectedElements)
{
	const uint32 NumHashBits = FMath::FloorLog2(NumExpectedElements);

	HashShift = 32 - NumHashBits;
	HashTable.Clear(1 << NumHashBits, NumExpectedElements);
	Updates.Reserve(NumExpectedElements);
}

void FOrderedScatterUpdater::ResolveOverwrites(bool bVerify)
{
	const uint32 NumUpdates = Updates.Num();

	uint32 WriteIndex = 0;
	for (uint32 ReadIndex = 0; ReadIndex < NumUpdates; ReadIndex++)
	{
		const FUpdate& Update = Updates[ReadIndex];

		const uint32 Offset = Update.GetOffset();

		// With multiplication information only travels from low bits to high bits, so pick the highest bits for hashing.
		const uint32 Hash = (Offset * 2654435761u) >> HashShift;

		uint32 NewIndex = HashTable.First(Hash);
		while (true)
		{
			if (!HashTable.IsValid(NewIndex))
			{
				NewIndex = WriteIndex++;
				HashTable.Add(Hash, NewIndex);
				break;
			}

			if (Offset == Updates[NewIndex].GetOffset())
			{
				if (bVerify)
				{
					const FUpdate& OldUpdate = Updates[NewIndex];
					const uint32 OldWriteMask = OldUpdate.WriteMask();
					const uint32 NewWriteMask = Update.WriteMask();
					// If the new update overwrites all bits written by the previous update, it is safe to discard the old update.
					check((OldWriteMask & ~NewWriteMask) == 0);
				}
				break;
			}

			NewIndex = HashTable.Next(NewIndex);
		}

		Updates[NewIndex] = Update;
	}

	Updates.SetNum(WriteIndex, EAllowShrinking::No);
}

void FOrderedScatterUpdater::Flush(FRDGBuilder& GraphBuilder, FRDGBufferUAV* UAV)
{
	if (Updates.Num() > 0)
	{
		static_assert(sizeof(FUpdate) == sizeof(FUintVector2), "FScatterUpdater::FUpdate must be the same size as FUintVector2");
		AddPass_ScatterUpdates(GraphBuilder, UAV, TConstArrayView<FUintVector2>((FUintVector2*)Updates.GetData(), Updates.Num()));

		HashTable.Clear();
		Updates.Reset();
	}
}

} // namespace Nanite