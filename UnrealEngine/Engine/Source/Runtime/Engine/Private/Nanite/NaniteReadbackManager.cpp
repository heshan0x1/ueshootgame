// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteReadbackManager.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ShaderCompilerCore.h"
#include "ShaderPermutationUtils.h"

#include "Nanite/NaniteStreamingShared.h"

namespace Nanite
{

static int32 GNaniteStreamingGPURequestsBufferMinSize = 64 * 1024;
static FAutoConsoleVariableRef CVarNaniteStreamingGPURequestsBufferMinSize(
	TEXT("r.Nanite.Streaming.GPURequestsBufferMinSize"),
	GNaniteStreamingGPURequestsBufferMinSize,
	TEXT("The minimum number of elements in the buffer used for GPU feedback.")
	TEXT("Setting Min=Max disables any dynamic buffer size adjustment."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingGPURequestsBufferMaxSize = 1024 * 1024;
static FAutoConsoleVariableRef CVarNaniteStreamingGPURequestsBufferMaxSize(
	TEXT("r.Nanite.Streaming.GPURequestsBufferMaxSize"),
	GNaniteStreamingGPURequestsBufferMaxSize,
	TEXT("The maximum number of elements in the buffer used for GPU feedback.")
	TEXT("Setting Min=Max disables any dynamic buffer size adjustment."),
	ECVF_RenderThreadSafe
);

class FClearStreamingRequestCount_CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearStreamingRequestCount_CS);
	SHADER_USE_PARAMETER_STRUCT(FClearStreamingRequestCount_CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FStreamingRequest>, OutStreamingRequests)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FClearStreamingRequestCount_CS, "/Engine/Private/Nanite/NaniteStreaming.usf", "ClearStreamingRequestCount", SF_Compute);

static void AddPass_ClearStreamingRequestCount(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef BufferUAVRef)
{
	// Need to always clear streaming requests on all GPUs.  We sometimes write to streaming request buffers on a mix of
	// GPU masks (shadow rendering on all GPUs, other passes on a single GPU), and we need to make sure all are clear
	// when they get used again.
	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	FClearStreamingRequestCount_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearStreamingRequestCount_CS::FParameters>();
	PassParameters->OutStreamingRequests = BufferUAVRef;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FClearStreamingRequestCount_CS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ClearStreamingRequestCount"),
		ComputeShader,
		PassParameters,
		FIntVector(1, 1, 1)
	);
}

FReadbackManager::FReadbackManager(uint32 InNumBuffers) :
	NumBuffers(InNumBuffers)
{
	ReadbackBuffers.SetNum(NumBuffers);
}

uint32 FReadbackManager::PrepareRequestsBuffer(FRDGBuilder& GraphBuilder)
{
	const uint32 BufferSize = RoundUpToSignificantBits(BufferSizeManager.GetSize(), 2);

	if (!RequestsBuffer.IsValid() || RequestsBuffer->Desc.NumElements != BufferSize)
	{
		// Init and clear StreamingRequestsBuffer.
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FGPUStreamingRequest), BufferSize);
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);
		FRDGBufferRef RequestsBufferRef = GraphBuilder.CreateBuffer(Desc, TEXT("Nanite.StreamingRequests"));

		AddPass_ClearStreamingRequestCount(GraphBuilder, GraphBuilder.CreateUAV(RequestsBufferRef));

		RequestsBuffer = GraphBuilder.ConvertToExternalBuffer(RequestsBufferRef);
	}
	return BufferSize;
}

FGPUStreamingRequest* FReadbackManager::LockLatest(uint32& OutNumStreamingRequestsClamped, uint32& OutNumStreamingRequests)
{
	OutNumStreamingRequestsClamped = 0u;
	OutNumStreamingRequests = 0u;
	check(LatestBuffer == nullptr);

	// Find latest buffer that is ready
	while (NumPendingBuffers > 0)
	{
		if (ReadbackBuffers[NextReadBufferIndex].Buffer->IsReady())
		{
			LatestBuffer = &ReadbackBuffers[NextReadBufferIndex];
			NextReadBufferIndex = (NextReadBufferIndex + 1u) % NumBuffers;
			NumPendingBuffers--;
		}
		else
		{
			break;
		}
	}

	if (LatestBuffer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LockBuffer);
		uint32* Ptr = (uint32*)LatestBuffer->Buffer->Lock(LatestBuffer->NumElements * sizeof(FGPUStreamingRequest));
		check(LatestBuffer->NumElements > 0u);

		const uint32 NumRequests = Ptr[0];
		BufferSizeManager.Update(NumRequests);

		OutNumStreamingRequests = NumRequests;

		OutNumStreamingRequestsClamped = FMath::Min(NumRequests, LatestBuffer->NumElements - 1u);
		return (FGPUStreamingRequest*)Ptr + 1;
	}
	return nullptr;
}

void FReadbackManager::Unlock()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UnlockBuffer);
	check(LatestBuffer);
	LatestBuffer->Buffer->Unlock();
	LatestBuffer = nullptr;
}

void FReadbackManager::QueueReadback(FRDGBuilder& GraphBuilder)
{
	if (NumPendingBuffers == NumBuffers)
	{
		// Return when queue is full. It is NOT safe to EnqueueCopy on a buffer that already has a pending copy.
		return;
	}

	if (!RequestsBuffer)
	{
		return;
	}

	const uint32 WriteBufferIndex = (NextReadBufferIndex + NumPendingBuffers) % NumBuffers;
	FReadbackBuffer& ReadbackBuffer = ReadbackBuffers[WriteBufferIndex];

	// Intentionally create a new FRHIGPUBufferReadback so its state is reset to !Ready to prevent a race with LockLatest
	// TODO: Optimize this
	ReadbackBuffer.Buffer = MakeUnique<FRHIGPUBufferReadback>(TEXT("Nanite.StreamingRequestReadback"));
	
	ReadbackBuffer.NumElements = RequestsBuffer->Desc.NumElements;

	FRDGBufferRef RDGRequestsBuffer = GraphBuilder.RegisterExternalBuffer(RequestsBuffer);

	AddReadbackBufferPass(GraphBuilder, RDG_EVENT_NAME("Readback"), RDGRequestsBuffer,
		[&GPUReadback = ReadbackBuffer.Buffer, RDGRequestsBuffer](FRDGAsyncTask, FRHICommandList& RHICmdList)
		{
			GPUReadback->EnqueueCopy(RHICmdList, RDGRequestsBuffer->GetRHI(), 0u);
		});

	AddPass_ClearStreamingRequestCount(GraphBuilder, GraphBuilder.CreateUAV(RDGRequestsBuffer));

	NumPendingBuffers++;
	BufferVersion++;
}

FRDGBuffer* FReadbackManager::GetStreamingRequestsBuffer(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.RegisterExternalBuffer(RequestsBuffer);
}

uint32 FReadbackManager::GetBufferVersion() const
{
	return BufferVersion;
}

FReadbackManager::FBufferSizeManager::FBufferSizeManager() :
	CurrentSize((float)GNaniteStreamingGPURequestsBufferMinSize)
{
}

void FReadbackManager::FBufferSizeManager::Update(uint32 NumRequests)
{
	const uint32 Target = uint32(NumRequests * 1.25f);			// Target 25% headroom

	const bool bOverBudget = Target > CurrentSize;
	const bool bUnderBudget = NumRequests < CurrentSize * 0.5f;	// Only consider shrinking when less than half the buffer is used

	OverBudgetCounter	= bOverBudget  ? (OverBudgetCounter  + 1u) : 0u;
	UnderBudgetCounter	= bUnderBudget ? (UnderBudgetCounter + 1u) : 0u;
			
	if (OverBudgetCounter >= 2u)		// Ignore single frames that are over budget
	{
		CurrentSize = FMath::Max(CurrentSize, Target);
	}
	else if (UnderBudgetCounter >= 30u)	// Only start shrinking when we have been under budget for a while
	{
		CurrentSize *= 0.98f;
	}

	const int32 LimitMinSize = 4u * 1024;
	const int32 LimitMaxSize = 1024u * 1024;
	const int32 MinSize = FMath::Clamp(GNaniteStreamingGPURequestsBufferMinSize, LimitMinSize, LimitMaxSize);
	const int32 MaxSize = FMath::Clamp(GNaniteStreamingGPURequestsBufferMaxSize, MinSize, LimitMaxSize);

	CurrentSize = FMath::Clamp(CurrentSize, (float)MinSize, (float)MaxSize);
}

uint32 FReadbackManager::FBufferSizeManager::GetSize()
{
	return uint32(CurrentSize);
}

} // namespace Nanite