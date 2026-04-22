// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteStreamingPageUploader.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "ShaderCompilerCore.h"
#include "ShaderPermutationUtils.h"

#include "Nanite/NaniteStreamingShared.h"

namespace Nanite
{

static TAutoConsoleVariable<int32> CVarNaniteStreamingTranscodeWaveSize(
	TEXT("r.Nanite.Streaming.TranscodeWaveSize"), 0,
	TEXT("Overrides the wave size to use for transcoding.\n")
	TEXT(" 0: Automatic (default);\n")
	TEXT(" 4: Wave size 4;\n")
	TEXT(" 8: Wave size 8;\n")
	TEXT(" 16: Wave size 16;\n")
	TEXT(" 32: Wave size 32;\n")
	TEXT(" 64: Wave size 64;\n")
	TEXT(" 128: Wave size 128;\n"),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingDynamicPageUploadBuffer = 0;
static FAutoConsoleVariableRef CVarNaniteStreamingDynamicPageUploadBuffer(
	TEXT("r.Nanite.Streaming.DynamicPageUploadBuffer"),
	GNaniteStreamingDynamicPageUploadBuffer,
	TEXT("Set Dynamic flag on the page upload buffer. This can eliminate a buffer copy on some platforms, but potentially also make the transcode shader slower."),
	ECVF_RenderThreadSafe
);


static int32 GNaniteStreamingAsyncCompute = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingAsyncCompute(
	TEXT("r.Nanite.Streaming.AsyncCompute"),
	GNaniteStreamingAsyncCompute,
	TEXT("Schedule GPU work in async compute queue."),
	ECVF_RenderThreadSafe
);


struct FPackedClusterInstallInfo
{
	uint32 LocalPageIndex_LocalClusterIndex;
	uint32 SrcPageOffset;
	uint32 DstPageOffset;
	uint32 PageDependenciesOffset;
};

class FTranscodePageToGPU_CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranscodePageToGPU_CS);
	SHADER_USE_PARAMETER_STRUCT(FTranscodePageToGPU_CS, FGlobalShader);

	class FTranscodePassDim : SHADER_PERMUTATION_SPARSE_INT("NANITE_TRANSCODE_PASS", NANITE_TRANSCODE_PASS_INDEPENDENT, NANITE_TRANSCODE_PASS_PARENT_DEPENDENT);
	class FGroupSizeDim : SHADER_PERMUTATION_SPARSE_INT("GROUP_SIZE", 4, 8, 16, 32, 64, 128);
	using FPermutationDomain = TShaderPermutationDomain<FTranscodePassDim, FGroupSizeDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32,													StartClusterIndex)
		SHADER_PARAMETER(uint32,													NumClusters)
		SHADER_PARAMETER(uint32,													ZeroUniform)
		SHADER_PARAMETER(FIntVector4,												PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedClusterInstallInfo>,ClusterInstallInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,						PageDependenciesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,							SrcPageBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer,						DstPageBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (!UE::ShaderPermutationUtils::ShouldCompileWithWaveSize(Parameters, PermutationVector.Get<FGroupSizeDim>()))
		{
			return false;
		}
		
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (!UE::ShaderPermutationUtils::ShouldPrecacheWithWaveSize(Parameters, PermutationVector.Get<FGroupSizeDim>()))
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		return FGlobalShader::ShouldPrecachePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);

		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
		OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
	}
};
IMPLEMENT_GLOBAL_SHADER(FTranscodePageToGPU_CS, "/Engine/Private/Nanite/NaniteTranscode.usf", "TranscodePageToGPU", SF_Compute);

static int32 SelectTranscodeWaveSize()
{
	const int32 WaveSizeOverride = CVarNaniteStreamingTranscodeWaveSize.GetValueOnRenderThread();

	int32 WaveSize = 0;
	if (WaveSizeOverride != 0 && WaveSizeOverride >= GRHIMinimumWaveSize && WaveSizeOverride <= GRHIMaximumWaveSize && FMath::IsPowerOfTwo(WaveSizeOverride))
	{
		WaveSize = WaveSizeOverride;
	}
	else if (IsRHIDeviceIntel() && 16 >= GRHIMinimumWaveSize && 16 <= GRHIMaximumWaveSize)
	{
		WaveSize = 16;
	}
	else
	{
		WaveSize = GRHIMaximumWaveSize;
	}

	return WaveSize;
}

FStreamingPageUploader::FStreamingPageUploader()
{
	ResetState();
}

void FStreamingPageUploader::Init(FRDGBuilder& GraphBuilder, uint32 InMaxPages, uint32 InMaxPageBytes, uint32 InMaxStreamingPages)
{
	ResetState();
	MaxPages = InMaxPages;
	MaxPageBytes = FMath::Max(InMaxPageBytes, 16u);
	MaxStreamingPages = InMaxStreamingPages;

	// Create a new set of buffers if the old set is already queued into RDG.
	if (IsRegistered(GraphBuilder, PageUploadBuffer))
	{
		PageUploadBuffer = nullptr;
		ClusterInstallInfoUploadBuffer = nullptr;
		PageDependenciesBuffer = nullptr;
	}

	const uint32 PageAllocationSize = FMath::RoundUpToPowerOfTwo(MaxPageBytes); //TODO: Revisit po2 rounding once upload buffer refactor lands
		
	// Add EBufferUsageFlags::Dynamic to skip the unneeded copy from upload to VRAM resource on d3d12 RHI
	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateByteAddressUploadDesc(PageAllocationSize);
	if (GNaniteStreamingDynamicPageUploadBuffer)
	{
		BufferDesc.Usage |= EBufferUsageFlags::Dynamic;
	}
		
	AllocatePooledBuffer(BufferDesc, PageUploadBuffer, TEXT("Nanite.PageUploadBuffer"));
	
	PageDataPtr = (uint8*)GraphBuilder.RHICmdList.LockBuffer(PageUploadBuffer->GetRHI(), 0, MaxPageBytes, RLM_WriteOnly);
}

uint8* FStreamingPageUploader::Add_GetRef(uint32 PageSize, uint32 NumClusters, uint32 DstPageOffset, const FPageKey& GPUPageKey, const TArray<uint32>& PageDependencies)
{
	check(IsAligned(PageSize, 4));
	check(IsAligned(DstPageOffset, 4));

	const uint32 PageIndex = AddedPageInfos.Num();

	check(PageIndex < MaxPages);
	check(NextPageByteOffset + PageSize <= MaxPageBytes);

	FAddedPageInfo& Info = AddedPageInfos.AddDefaulted_GetRef();
	Info.GPUPageKey = GPUPageKey;
	Info.SrcPageOffset = NextPageByteOffset;
	Info.DstPageOffset = DstPageOffset;
	Info.PageDependenciesOffset = FlattenedPageDependencies.Num();
	Info.NumPageDependencies = PageDependencies.Num();
	Info.ClustersOffset = NextClusterIndex;
	Info.NumClusters = NumClusters;
	Info.InstallPassIndex = 0xFFFFFFFFu;
	FlattenedPageDependencies.Append(PageDependencies);
	GPUPageKeyToAddedIndex.Add(GPUPageKey, PageIndex);

	uint8* ResultPtr = PageDataPtr + NextPageByteOffset;
	NextPageByteOffset += PageSize;
	NextClusterIndex += NumClusters;
		
	return ResultPtr;
}

void FStreamingPageUploader::Release()
{
	ClusterInstallInfoUploadBuffer.SafeRelease();
	PageUploadBuffer.SafeRelease();
	PageDependenciesBuffer.SafeRelease();
	ResetState();
}

void FStreamingPageUploader::ResourceUploadTo(FRDGBuilder& GraphBuilder, FRDGBuffer* DstBuffer)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::Transcode");
	GraphBuilder.RHICmdList.UnlockBuffer(PageUploadBuffer->GetRHI());

	const uint32 NumPages = AddedPageInfos.Num();
	if (NumPages == 0)	// This can end up getting called with NumPages = 0 when NumReadyPages > 0 and all pages early out.
	{
		ResetState();
		return;
	}

	const uint32 ClusterInstallInfoAllocationSize = FMath::RoundUpToPowerOfTwo(NextClusterIndex * sizeof(FPackedClusterInstallInfo));
	if (ClusterInstallInfoAllocationSize > TryGetSize(ClusterInstallInfoUploadBuffer))
	{
		const uint32 BytesPerElement = sizeof(FPackedClusterInstallInfo);

		AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredUploadDesc(BytesPerElement, ClusterInstallInfoAllocationSize / BytesPerElement), ClusterInstallInfoUploadBuffer, TEXT("Nanite.ClusterInstallInfoUploadBuffer"));
	}

	FPackedClusterInstallInfo* ClusterInstallInfoPtr = (FPackedClusterInstallInfo*)GraphBuilder.RHICmdList.LockBuffer(ClusterInstallInfoUploadBuffer->GetRHI(), 0, ClusterInstallInfoAllocationSize, RLM_WriteOnly);

	uint32 PageDependenciesAllocationSize = FMath::RoundUpToPowerOfTwo(FMath::Max(FlattenedPageDependencies.Num(), 4096) * sizeof(uint32));
	if (PageDependenciesAllocationSize > TryGetSize(PageDependenciesBuffer))
	{
		const uint32 BytesPerElement = sizeof(uint32);

		AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredUploadDesc(BytesPerElement, PageDependenciesAllocationSize / BytesPerElement), PageDependenciesBuffer, TEXT("Nanite.PageDependenciesBuffer"));
	}

	uint32* PageDependenciesPtr = (uint32*)GraphBuilder.RHICmdList.LockBuffer(PageDependenciesBuffer->GetRHI(), 0, PageDependenciesAllocationSize, RLM_WriteOnly);
	FMemory::Memcpy(PageDependenciesPtr, FlattenedPageDependencies.GetData(), FlattenedPageDependencies.Num() * sizeof(uint32));
	GraphBuilder.RHICmdList.UnlockBuffer(PageDependenciesBuffer->GetRHI());

	// Split page installs into passes.
	// Every pass adds the pages that no longer have any unresolved dependency.
	// Essentially a naive multi-pass topology sort, but with a low number of passes in practice.
	check(PassInfos.Num() == 0);
	uint32 NumRemainingPages = NumPages;
	uint32 NumClusters = 0;
	uint32 NextSortedPageIndex = 0;
	while (NumRemainingPages > 0)
	{
		const uint32 CurrentPassIndex = PassInfos.Num();
		uint32 NumPassPages = 0;
		uint32 NumPassClusters = 0;
		for(FAddedPageInfo& PageInfo : AddedPageInfos)
		{
			if (PageInfo.InstallPassIndex < CurrentPassIndex)
				continue;	// Page already installed in an earlier pass

			bool bMissingDependency = false;
			for (uint32 i = 0; i < PageInfo.NumPageDependencies; i++)
			{
				const uint32 GPUPageIndex = FlattenedPageDependencies[PageInfo.PageDependenciesOffset + i];
				const FPageKey DependencyGPUPageKey = { PageInfo.GPUPageKey.RuntimeResourceID, GPUPageIndex };
				const uint32* DependencyAddedIndexPtr = GPUPageKeyToAddedIndex.Find(DependencyGPUPageKey);

				// Check if a dependency has not yet been installed.
				// We only need to resolve dependencies in the current batch. Batches are already ordered.
				if (DependencyAddedIndexPtr && AddedPageInfos[*DependencyAddedIndexPtr].InstallPassIndex >= CurrentPassIndex)
				{
					bMissingDependency = true;
					break;
				}
			}

			if (!bMissingDependency)
			{
				PageInfo.InstallPassIndex = CurrentPassIndex;

				// Add cluster infos
				check(PageInfo.NumClusters <= NANITE_MAX_CLUSTERS_PER_PAGE);
				for(uint32 i = 0; i < PageInfo.NumClusters; i++)
				{
					ClusterInstallInfoPtr->LocalPageIndex_LocalClusterIndex = (NextSortedPageIndex << NANITE_MAX_CLUSTERS_PER_PAGE_BITS) | i;
					ClusterInstallInfoPtr->SrcPageOffset					= PageInfo.SrcPageOffset;
					ClusterInstallInfoPtr->DstPageOffset					= PageInfo.DstPageOffset;
					ClusterInstallInfoPtr->PageDependenciesOffset			= PageInfo.PageDependenciesOffset;
					ClusterInstallInfoPtr++;
				}
				NextSortedPageIndex++;
				NumPassPages++;
				NumPassClusters += PageInfo.NumClusters;
			}
		}

		FPassInfo PassInfo;
		PassInfo.NumPages = NumPassPages;
		PassInfo.NumClusters = NumPassClusters;
		PassInfos.Add(PassInfo);
		NumRemainingPages -= NumPassPages;
	}

	GraphBuilder.RHICmdList.UnlockBuffer(ClusterInstallInfoUploadBuffer->GetRHI());

	FRDGBufferSRV* PageUploadBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PageUploadBuffer));
	FRDGBufferSRV* ClusterInstallInfoUploadBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(ClusterInstallInfoUploadBuffer));
	FRDGBufferSRV* PageDependenciesBufferSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(PageDependenciesBuffer));
	FRDGBufferUAV* DstBufferUAV = GraphBuilder.CreateUAV(DstBuffer);

	// Disable async compute for streaming systems when MGPU is active, to work around GPU hangs
	const bool bAsyncCompute = GSupportsEfficientAsyncCompute && (GNaniteStreamingAsyncCompute != 0) && (GNumExplicitGPUsForRendering == 1);
		
	check(GRHISupportsWaveOperations);

	const uint32 PreferredGroupSize = (uint32)SelectTranscodeWaveSize();

	FTranscodePageToGPU_CS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FTranscodePageToGPU_CS::FGroupSizeDim>(PreferredGroupSize);

	// Independent transcode
	{
		FTranscodePageToGPU_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranscodePageToGPU_CS::FParameters>();
		PassParameters->ClusterInstallInfoBuffer	= ClusterInstallInfoUploadBufferSRV;
		PassParameters->PageDependenciesBuffer		= PageDependenciesBufferSRV;
		PassParameters->SrcPageBuffer				= PageUploadBufferSRV;
		PassParameters->DstPageBuffer				= DstBufferUAV;
		PassParameters->StartClusterIndex			= 0;
		PassParameters->NumClusters					= NextClusterIndex;
		PassParameters->ZeroUniform					= 0;
		PassParameters->PageConstants				= FIntVector4(0, MaxStreamingPages, 0, 0);

		PermutationVector.Set<FTranscodePageToGPU_CS::FTranscodePassDim>(NANITE_TRANSCODE_PASS_INDEPENDENT);
		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FTranscodePageToGPU_CS>(PermutationVector);
			
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TranscodePageToGPU Independent (ClusterCount: %u, GroupSize: %u)", NextClusterIndex, PreferredGroupSize),
			bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(NextClusterIndex));
	}

	// Parent-dependent transcode
	const uint32 NumPasses = PassInfos.Num();
	uint32 StartClusterIndex = 0;

	for (uint32 PassIndex = 0; PassIndex < NumPasses; PassIndex++)
	{
		const FPassInfo& PassInfo = PassInfos[PassIndex];

		FTranscodePageToGPU_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranscodePageToGPU_CS::FParameters>();
		PassParameters->ClusterInstallInfoBuffer	= ClusterInstallInfoUploadBufferSRV;
		PassParameters->PageDependenciesBuffer		= PageDependenciesBufferSRV;
		PassParameters->SrcPageBuffer				= PageUploadBufferSRV;
		PassParameters->DstPageBuffer				= DstBufferUAV;
		PassParameters->StartClusterIndex			= StartClusterIndex;
		PassParameters->NumClusters					= PassInfo.NumClusters;
		PassParameters->ZeroUniform					= 0;
		PassParameters->PageConstants				= FIntVector4(0, MaxStreamingPages, 0, 0);
			
		PermutationVector.Set<FTranscodePageToGPU_CS::FTranscodePassDim>(NANITE_TRANSCODE_PASS_PARENT_DEPENDENT);
		auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FTranscodePageToGPU_CS>(PermutationVector);
			
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TranscodePageToGPU Dependent (ClusterOffset: %u, ClusterCount: %u, GroupSize: %u)", StartClusterIndex, PassInfo.NumClusters, PreferredGroupSize),
			bAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCountWrapped(PassInfo.NumClusters));

		StartClusterIndex += PassInfo.NumClusters;
	}	
	Release();
}

void FStreamingPageUploader::ResetState()
{
	PageDataPtr = nullptr;
	MaxPages = 0;
	MaxPageBytes = 0;
	NextPageByteOffset = 0;
	NextClusterIndex = 0;
	AddedPageInfos.Reset();
	GPUPageKeyToAddedIndex.Reset();
	FlattenedPageDependencies.Reset();
	PassInfos.Reset();
}

} // namespace Nanite