// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/NaniteStreamingManager.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Async/ParallelFor.h"
#include "RenderUtils.h"
#include "Rendering/NaniteResources.h"
#include "ShaderCompilerCore.h"
#include "Stats/StatsTrace.h"
#include "HAL/PlatformFileManager.h"
#include "ShaderPermutationUtils.h"
#include "ProfilingDebugging/IoStoreTrace.h"
#include "Nanite/NaniteFixupChunk.h"
#include "Nanite/NaniteOrderedScatterUpdater.h"
#include "Nanite/NaniteStreamingShared.h"
#include "Nanite/NaniteReadbackManager.h"
#include "Nanite/NaniteStreamingPageUploader.h"

#if WITH_EDITOR
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
using namespace UE::DerivedData;
#endif

#define MAX_LEGACY_REQUESTS_PER_UPDATE		32u		// Legacy IO requests are slow and cause lots of bubbles, so we NEED to limit them.

#define MAX_RUNTIME_RESOURCE_VERSIONS_BITS	8								// Just needs to be large enough to cover maximum number of in-flight versions
#define MAX_RUNTIME_RESOURCE_VERSIONS_MASK	((1 << MAX_RUNTIME_RESOURCE_VERSIONS_BITS) - 1)	

#define MAX_RESOURCE_PREFETCH_PAGES			16

#define LRU_INDEX_MASK						0x7FFFFFFFu
#define LRU_FLAG_REFERENCED_THIS_UPDATE		0x80000000u

#define DEBUG_TRANSCODE_PAGES_REPEATEDLY	0	// TODO: Fix this debug mode
#define DEBUG_ALLOCATION_STRESS_TEST		0

static int32 GNaniteStreamingAsync = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingAsync(
	TEXT("r.Nanite.Streaming.Async"),
	GNaniteStreamingAsync,
	TEXT("Perform most of the Nanite streaming on an asynchronous worker thread instead of the rendering thread."),
	ECVF_RenderThreadSafe
);

static float GNaniteStreamingBandwidthLimit = -1.0f;
static FAutoConsoleVariableRef CVarNaniteStreamingBandwidthLimit(
	TEXT("r.Nanite.Streaming.BandwidthLimit" ),
	GNaniteStreamingBandwidthLimit,
	TEXT("Streaming bandwidth limit in megabytes per second. Negatives values are interpreted as unlimited. "),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingPoolSize = 512;
static FAutoConsoleVariableRef CVarNaniteStreamingPoolSize(
	TEXT("r.Nanite.Streaming.StreamingPoolSize"),
	GNaniteStreamingPoolSize,
	TEXT("Size of streaming pool in MB. Does not include memory used for root pages.")
	TEXT("Be careful with setting this close to the GPU resource size limit (typically 2-4GB) as root pages are allocated from the same physical buffer."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingNumInitialRootPages = 2048;
static FAutoConsoleVariableRef CVarNaniteStreamingNumInitialRootPages(
	TEXT("r.Nanite.Streaming.NumInitialRootPages"),
	GNaniteStreamingNumInitialRootPages,
	TEXT("Number of root pages in initial allocation. Allowed to grow on demand if r.Nanite.Streaming.DynamicallyGrowAllocations is enabled."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingNumInitialImposters = 2048;
static FAutoConsoleVariableRef CVarNaniteStreamingNumInitialImposters(
	TEXT("r.Nanite.Streaming.NumInitialImposters"),
	GNaniteStreamingNumInitialImposters,
	TEXT("Number of imposters in initial allocation. Allowed to grow on demand if r.Nanite.Streaming.DynamicallyGrowAllocations is enabled."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingDynamicallyGrowAllocations = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingDynamicallyGrowAllocations(
	TEXT("r.Nanite.Streaming.DynamicallyGrowAllocations"),
	GNaniteStreamingDynamicallyGrowAllocations,
	TEXT("Determines if root page and imposter allocations are allowed to grow dynamically from initial allocation set by r.Nanite.Streaming.NumInitialRootPages and r.Nanite.Streaming.NumInitialImposters"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingMaxPendingPages = 128;
static FAutoConsoleVariableRef CVarNaniteStreamingMaxPendingPages(
	TEXT("r.Nanite.Streaming.MaxPendingPages"),
	GNaniteStreamingMaxPendingPages,
	TEXT("Maximum number of pages that can be pending for installation."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingImposters = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingImposters(
	TEXT("r.Nanite.Streaming.Imposters"),
	GNaniteStreamingImposters,
	TEXT("Load imposters used for faster rendering of distant objects. Requires additional memory and might not be worthwhile for scenes with HLOD or no distant objects."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingMaxPageInstallsPerFrame = 128;
static FAutoConsoleVariableRef CVarNaniteStreamingMaxPageInstallsPerFrame(
	TEXT("r.Nanite.Streaming.MaxPageInstallsPerFrame"),
	GNaniteStreamingMaxPageInstallsPerFrame,
	TEXT("Maximum number of pages that can be installed per frame. Limiting this can limit the overhead of streaming."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingNumRetries = 3;
static FAutoConsoleVariableRef CVarNaniteStreamingNumRetries(
	TEXT("r.Nanite.Streaming.NumRetries"),
	GNaniteStreamingNumRetries,
	TEXT("Number of times to retry an IO or DDC request on failure."),
	ECVF_RenderThreadSafe
);

// Controls for dynamically adjusting quality (pixels per edge) when the streaming pool is being overcommitted.
// This should be a rare condition in practice, but can happen when rendering scenes with lots of unique geometry at high resolutions.
static float GNaniteStreamingQualityScaleMinPoolPercentage = 70.0f;
static FAutoConsoleVariableRef CVarNaniteStreamingQualityScaleMinPoolPercentage(
	TEXT("r.Nanite.Streaming.QualityScale.MinPoolPercentage"),
	GNaniteStreamingQualityScaleMinPoolPercentage,
	TEXT("Adjust quality up whenever the streaming pool load percentage goes below this threshold."),
	ECVF_RenderThreadSafe
);

static float GNaniteStreamingQualityScaleMaxPoolPercentage = 85.0f;
static FAutoConsoleVariableRef CVarNaniteStreamingQualityScaleMaxPoolPercentage(
	TEXT("r.Nanite.Streaming.QualityScale.MaxPoolPercentage"),
	GNaniteStreamingQualityScaleMaxPoolPercentage,
	TEXT("Adjust quality down whenever the streaming pool load percentage goes above this threshold."),
	ECVF_RenderThreadSafe
);

static float GNaniteStreamingQualityScaleMinQuality = 0.3f;
static FAutoConsoleVariableRef CVarNaniteStreamingQualityScaleMinQuality(
	TEXT("r.Nanite.Streaming.QualityScale.MinQuality"),
	GNaniteStreamingQualityScaleMinQuality,
	TEXT("Quality scaling will never go below this limit. 1.0 disables any scaling."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingExplicitRequests = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingExplicitRequests(
	TEXT("r.Nanite.Streaming.Debug.ExplicitRequests"),
	GNaniteStreamingExplicitRequests,
	TEXT("Process requests coming from explicit calls to RequestNanitePages()."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingGPURequests = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingGPUFeedback(
	TEXT("r.Nanite.Streaming.Debug.GPURequests"),
	GNaniteStreamingGPURequests,
	TEXT("Process requests coming from GPU rendering feedback"),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingPrefetch = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingPrefetch(
	TEXT("r.Nanite.Streaming.Debug.Prefetch"),
	GNaniteStreamingPrefetch,
	TEXT("Process resource prefetch requests from calls to PrefetchResource()."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingPoolResize = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingPoolResize(
	TEXT("r.Nanite.Streaming.Debug.StreamingPoolResize"),
	GNaniteStreamingPoolResize,
	TEXT("Allow streaming pool to be resized at runtime."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingReservedResourceIgnoreInitialRootAllocation = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingReservedResourceIgnoreInitialRootAllocation(
	TEXT("r.Nanite.Streaming.Debug.ReservedResourceIgnoreInitialRootAllocation"),
	GNaniteStreamingReservedResourceIgnoreInitialRootAllocation,
	TEXT("Ignore root page initial allocation size for reserved resources."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingReservedResourceRootPageGrowOnly = 0;
static FAutoConsoleVariableRef CVarNaniteStreamingReservedResourceGrowOnly(
	TEXT("r.Nanite.Streaming.Debug.ReservedResourceRootPageGrowOnly"),
	GNaniteStreamingReservedResourceRootPageGrowOnly,
	TEXT("Root page allocator only grows."),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

static int32 GNaniteStreamingVerificationLevel = 1;
static FAutoConsoleVariableRef CVarNaniteStreamingVerificationLevel(
	TEXT("r.Nanite.Streaming.Debug.VerificationLevel"),
	GNaniteStreamingVerificationLevel,
	TEXT("Additional debug verification. 0: Off, 1: Light, 2: Heavy."),
	ECVF_RenderThreadSafe
);

static int32 GNaniteStreamingReservedResources = 0;
static FAutoConsoleVariableRef CVarNaniteStreamingReservedResources(
	TEXT("r.Nanite.Streaming.ReservedResources"),
	GNaniteStreamingReservedResources,
	TEXT("Allow allocating Nanite GPU resources as reserved resources for better memory utilization and more efficient resizing (EXPERIMENTAL)"),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static bool bPendingPoolReset = false;
static FAutoConsoleCommand CVarYourCommand(
	TEXT("r.Nanite.Streaming.ResetStreamingPool"),
	TEXT("Resets the Nanite streaming pool on next update."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
		bPendingPoolReset = true;
	}),
	ECVF_Default
);

static_assert(NANITE_MAX_GPU_PAGES_BITS + MAX_RUNTIME_RESOURCE_VERSIONS_BITS + NANITE_STREAMING_REQUEST_MAGIC_BITS <= 32,		"Streaming request member RuntimeResourceID_Magic doesn't fit in 32 bits");
static_assert(NANITE_MAX_RESOURCE_PAGES_BITS + NANITE_MAX_GROUP_PARTS_BITS + NANITE_STREAMING_REQUEST_MAGIC_BITS <= 32,			"Streaming request member PageIndex_NumPages_Magic doesn't fit in 32 bits");

DECLARE_STATS_GROUP_SORTBYNAME(	TEXT("NaniteStreaming"),					STATGROUP_NaniteStreaming,								STATCAT_Advanced);

DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("Nanite Resources"),					STAT_NaniteStreaming00_NaniteResources,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("Imposters"),							STAT_NaniteStreaming01_Imposters,						STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("HierarchyNodes"),						STAT_NaniteStreaming02_HierarchyNodes,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("    Peak"),							STAT_NaniteStreaming03_PeakHierarchyNodes,				STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("    Max Depth"),						STAT_NaniteStreaming04_MaxHierarchyLevels,				STATGROUP_NaniteStreaming);

DECLARE_DWORD_ACCUMULATOR_STAT( TEXT("Root Pages"),							STAT_NaniteStreaming06_RootPages,						STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("    Peak"),							STAT_NaniteStreaming07_PeakRootPages,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("    Allocated"),						STAT_NaniteStreaming08_AllocatedRootPages,				STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("    Limit"),							STAT_NaniteStreaming09_RootPageLimit,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("Streaming Pool Pages"),				STAT_NaniteStreaming0A_StreamingPoolPages,				STATGROUP_NaniteStreaming);
DECLARE_DWORD_ACCUMULATOR_STAT(	TEXT("Total Streaming Pages"),				STAT_NaniteStreaming0B_TotalStreamingPages,				STATGROUP_NaniteStreaming);

DECLARE_FLOAT_ACCUMULATOR_STAT( TEXT("Imposter Size (MB)"),					STAT_NaniteStreaming10_ImpostersSizeMB,					STATGROUP_NaniteStreaming);
DECLARE_FLOAT_ACCUMULATOR_STAT(	TEXT("Hierarchy Size (MB)"),				STAT_NaniteStreaming11_HiearchySizeMB,					STATGROUP_NaniteStreaming);

DECLARE_FLOAT_ACCUMULATOR_STAT(	TEXT("Total Pool Size (MB)"),				STAT_NaniteStreaming12_TotalPoolSizeMB,					STATGROUP_NaniteStreaming);
DECLARE_FLOAT_ACCUMULATOR_STAT(	TEXT("    Root Pool Size (MB)"),			STAT_NaniteStreaming13_AllocatedRootPagesSizeMB,		STATGROUP_NaniteStreaming);
DECLARE_FLOAT_ACCUMULATOR_STAT(	TEXT("    Streaming Pool Size (MB)"),		STAT_NaniteStreaming14_StreamingPoolSizeMB,				STATGROUP_NaniteStreaming);
DECLARE_FLOAT_ACCUMULATOR_STAT(	TEXT("Total Pool Size Limit (MB)"),			STAT_NaniteStreaming15_TotalPoolSizeLimitMB,			STATGROUP_NaniteStreaming);

DECLARE_DWORD_COUNTER_STAT(		TEXT("Page Requests"),						STAT_NaniteStreaming20_PageRequests,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    GPU"),							STAT_NaniteStreaming21_PageRequestsGPU,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    Explicit"),						STAT_NaniteStreaming22_PageRequestsExplicit,			STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    Prefetch"),						STAT_NaniteStreaming23_PageRequestsPrefetch,			STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    Parents"),						STAT_NaniteStreaming24_PageRequestsParents,				STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    Unique"),							STAT_NaniteStreaming25_PageRequestsUnique,				STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    Registered"),						STAT_NaniteStreaming26_PageRequestsRegistered,			STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("    New"),							STAT_NaniteStreaming27_PageRequestsNew,					STATGROUP_NaniteStreaming);

DECLARE_FLOAT_COUNTER_STAT(		TEXT("Visible Streaming Data Size (MB)"),	STAT_NaniteStreaming30_VisibleStreamingDataSizeMB,		STATGROUP_NaniteStreaming);
DECLARE_FLOAT_COUNTER_STAT(		TEXT("    Streaming Pool Percentage"),		STAT_NaniteStreaming31_VisibleStreamingPoolPercentage,	STATGROUP_NaniteStreaming);
DECLARE_FLOAT_COUNTER_STAT(		TEXT("    Quality Scale"),					STAT_NaniteStreaming32_VisibleStreamingQualityScale,	STATGROUP_NaniteStreaming);


DECLARE_FLOAT_COUNTER_STAT(		TEXT("IO Request Size (MB)"),				STAT_NaniteStreaming40_IORequestSizeMB,					STATGROUP_NaniteStreaming);

DECLARE_DWORD_COUNTER_STAT(		TEXT("Readback Size"),						STAT_NaniteStreaming41_ReadbackSize,					STATGROUP_NaniteStreaming);
DECLARE_DWORD_COUNTER_STAT(		TEXT("Readback Buffer Size"),				STAT_NaniteStreaming42_ReadbackBufferSize,				STATGROUP_NaniteStreaming);


DECLARE_CYCLE_STAT(				TEXT("AddResource"),						STAT_NaniteStreaming_AddResource,						STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("RemoveResource"),						STAT_NaniteStreaming_RemoveResource,					STATGROUP_NaniteStreaming);

DECLARE_CYCLE_STAT(				TEXT("BeginAsyncUpdate"),					STAT_NaniteStreaming_BeginAsyncUpdate,					STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("AsyncUpdate"),						STAT_NaniteStreaming_AsyncUpdate,						STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("ProcessRequests"),					STAT_NaniteStreaming_ProcessRequests,					STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("InstallReadyPages"),					STAT_NaniteStreaming_InstallReadyPages,					STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("UploadTask"),							STAT_NaniteStreaming_UploadTask,						STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("ApplyFixup"),							STAT_NaniteStreaming_ApplyFixup,						STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("ResolveOverwrites"),					STAT_NaniteStreaming_ResolveOverwrites,					STATGROUP_NaniteStreaming);

DECLARE_CYCLE_STAT(				TEXT("EndAsyncUpdate"),						STAT_NaniteStreaming_EndAsyncUpdate,					STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("AddParentRequests"),					STAT_NaniteStreaming_AddParentRequests,					STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("AddParentRegisteredRequests"),		STAT_NaniteStreaming_AddParentRegisteredRequests,		STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("AddParentNewRequests"),				STAT_NaniteStreaming_AddParentNewRequests,				STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("ClearReferencedArray"),				STAT_NaniteStreaming_ClearReferencedArray,				STATGROUP_NaniteStreaming);

DECLARE_CYCLE_STAT(				TEXT("CompactLRU"),							STAT_NaniteStreaming_CompactLRU,						STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("UpdateLRU"),							STAT_NaniteStreaming_UpdateLRU,							STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("ProcessGPURequests"),					STAT_NaniteStreaming_ProcessGPURequests,				STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("SelectHighestPriority"),				STAT_NaniteStreaming_SelectHighestPriority,				STATGROUP_NaniteStreaming);

DECLARE_CYCLE_STAT(				TEXT("Heapify"),							STAT_NaniteStreaming_Heapify,							STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("VerifyLRU"),							STAT_NaniteStreaming_VerifyLRU,							STATGROUP_NaniteStreaming);
DECLARE_CYCLE_STAT(				TEXT("VerifyFixupState"),					STAT_NaniteStreaming_VerifyFixupState,					STATGROUP_NaniteStreaming);


DECLARE_LOG_CATEGORY_EXTERN(LogNaniteStreaming, Log, All);
DEFINE_LOG_CATEGORY(LogNaniteStreaming);

CSV_DEFINE_CATEGORY(NaniteStreaming, true);
CSV_DEFINE_CATEGORY(NaniteStreamingDetail, false);

namespace Nanite
{

FORCEINLINE int32 VerificationLevel()
{
#if DO_CHECK
	return GNaniteStreamingVerificationLevel;
#else
	return 0;
#endif
}

#if WITH_EDITOR
	const FValueId NaniteValueId = FValueId::FromName("NaniteStreamingData");
#endif

static uint32 GetMaxPagePoolSizeInMB()
{
	const uint32 DesiredSizeInMB = IsRHIDeviceAMD() ? 4095 : 2048;
	const uint32 MaxSizeInMB = (uint32)(GRHIGlobals.MaxViewSizeBytesForNonTypedBuffer >> 20);
	return FMath::Min(DesiredSizeInMB, MaxSizeInMB);
}

class FMemcpy_CS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMemcpy_CS);
	SHADER_USE_PARAMETER_STRUCT(FMemcpy_CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, SrcOffset)
		SHADER_PARAMETER(uint32, DstOffset)
		SHADER_PARAMETER(uint32, NumThreads)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, DstBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FMemcpy_CS, "/Engine/Private/Nanite/NaniteStreaming.usf", "Memcpy", SF_Compute);

// Can't use AddCopyBufferPass because it doesn't support dst==src
static void AddPass_Memcpy(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef UAV, uint32 DstOffset, uint32 SrcOffset, uint32 Length)
{
	check(Length >= NANITE_ROOT_PAGE_GPU_SIZE);
	check(SrcOffset >= DstOffset + Length || DstOffset >= SrcOffset + Length);
	
	check((DstOffset & 15u) == 0u);
	check((SrcOffset & 15u) == 0u);
	check((Length & 15u) == 0u);

	const uint32 NumThreads = Length >> 4;

	FMemcpy_CS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMemcpy_CS::FParameters>();
	PassParameters->SrcOffset	= SrcOffset;
	PassParameters->DstOffset	= DstOffset;
	PassParameters->NumThreads	= NumThreads;
	PassParameters->DstBuffer	= UAV;

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FMemcpy_CS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Memcpy"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCountWrapped(NumThreads, 64)
		);
}

static void AddPass_Memmove(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef UAV, uint32 DstOffset, uint32 SrcOffset, uint32 Length)
{
	if (DstOffset == SrcOffset)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "Memmove");
	const uint32 DeltaOffset  = (DstOffset > SrcOffset) ? (DstOffset - SrcOffset) : (SrcOffset - DstOffset);
	const uint32 MaxBlockSize = FMath::Min(FMath::Min(Length, DeltaOffset), 16u << 20);
	const bool	 bReverseCopy = (DstOffset > SrcOffset);

	for (uint32 i = 0; i < Length; i += MaxBlockSize)
	{
		const uint32 BlockSize	= FMath::Min(Length - i, MaxBlockSize);
		const uint32 Offset		= bReverseCopy ? (Length - i - BlockSize) : i;

		AddPass_Memcpy(GraphBuilder, UAV, DstOffset + Offset, SrcOffset + Offset, BlockSize);
	}
}

class FHierarchyDepthManager
{
public:
	FHierarchyDepthManager(uint32 MaxDepth)
	{
		DepthHistogram.SetNumZeroed(MaxDepth + 1);
	}

	void Add(uint32 Depth)
	{
		DepthHistogram[Depth]++;
	}
	void Remove(uint32 Depth)
	{
		uint32& Count = DepthHistogram[Depth];
		check(Count > 0u);
		Count--;
	}

	uint32 CalculateNumLevels() const
	{
		for (int32 Depth = uint32(DepthHistogram.Num() - 1); Depth >= 0; Depth--)
		{
			if (DepthHistogram[Depth] != 0u)
			{
				return uint32(Depth) + 1u;
			}
		}
		return 0u;
	}
private:
	TArray<uint32> DepthHistogram;
};

class FRingBufferAllocator
{
public:
	FRingBufferAllocator(uint32 Size):
		BufferSize(Size)
	{
		Reset();
	}

	void Reset()
	{
		ReadOffset = 0u;
		WriteOffset = 0u;
	#if DO_CHECK
		SizeQueue.Empty();
	#endif
	}

	bool TryAllocate(uint32 Size, uint32& AllocatedOffset)
	{
		if (WriteOffset < ReadOffset)
		{
			if (Size + 1u > ReadOffset - WriteOffset)	// +1 to leave one element free, so we can distinguish between full and empty
			{
				return false;
			}
		}
		else
		{
			// WriteOffset >= ReadOffset
			if (Size + (ReadOffset == 0u ? 1u : 0u) > BufferSize - WriteOffset)
			{
				// Doesn't fit at the end. Try from the beginning
				if (Size + 1u > ReadOffset)
				{
					return false;
				}
				WriteOffset = 0u;
			}
		}

	#if DO_CHECK
		SizeQueue.Enqueue(Size);
	#endif
		AllocatedOffset = WriteOffset;
		WriteOffset += Size;
		check(AllocatedOffset + Size <= BufferSize);
		return true;
	}

	void Free(uint32 Size)
	{
	#if DO_CHECK
		uint32 QueuedSize;
		bool bNonEmpty = SizeQueue.Dequeue(QueuedSize);
		check(bNonEmpty);
		check(QueuedSize == Size);
	#endif
		const uint32 Next = ReadOffset + Size;
		ReadOffset = (Next <= BufferSize) ? Next : Size;
	}
private:
	uint32 BufferSize;
	uint32 ReadOffset;
	uint32 WriteOffset;
#if DO_CHECK
	TQueue<uint32> SizeQueue;
#endif
};

class FQualityScalingManager
{
public:
	float Update(float StreamingPoolPercentage)
	{
		const float MinPercentage = FMath::Clamp(GNaniteStreamingQualityScaleMinPoolPercentage, 10.0f, 100.0f);
		const float MaxPercentage = FMath::Clamp(GNaniteStreamingQualityScaleMaxPoolPercentage, MinPercentage, 100.0f);

		const bool bOverBudget = (StreamingPoolPercentage > MaxPercentage);
		const bool bUnderBudget = (StreamingPoolPercentage < MinPercentage);

		OverBudgetCounter = bOverBudget ? (OverBudgetCounter + 1u) : 0u;
		UnderBudgetCounter = bUnderBudget ? (UnderBudgetCounter + 1u) : 0u;

		if (OverBudgetCounter >= 2u)
		{
			// Ignore single frames that could be because of temporary disocclusion.
			// When we are over budget for more than on frame, adjust quality down rapidly.
			Scale *= 0.97f;
		}
		else if (UnderBudgetCounter >= 30u)
		{
			// If we are under budget, slowly start increasing quality again.
			Scale *= 1.01f;
		}

		const float MinScale = FMath::Clamp(GNaniteStreamingQualityScaleMinQuality, 0.1f, 1.0f);
		Scale = FMath::Clamp(Scale, MinScale, 1.0f);
		return Scale;
	}
private:
	float Scale = 1.0f;
	uint32 OverBudgetCounter = 0u;
	uint32 UnderBudgetCounter = 0u;
};

FStreamingManager::FStreamingManager()
#if WITH_EDITOR
	: RequestOwner(nullptr)
#endif
{
}

void FStreamingManager::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	LLM_SCOPE_BYTAG(Nanite);

	HierarchyDepthManager = MakePimpl<FHierarchyDepthManager>(NANITE_MAX_CLUSTER_HIERARCHY_DEPTH);
	ReadbackManager = MakePimpl<FReadbackManager>(4);
	QualityScalingManager = MakePimpl<FQualityScalingManager>();

	UpdatePageConfiguration();

	MaxPendingPages = GNaniteStreamingMaxPendingPages;
	MaxPageInstallsPerUpdate = (uint32)FMath::Min(GNaniteStreamingMaxPageInstallsPerFrame, GNaniteStreamingMaxPendingPages);

	PendingPageStagingMemory.SetNumUninitialized(MaxPendingPages * NANITE_ESTIMATED_MAX_PAGE_DISK_SIZE);
	PendingPageStagingAllocator = MakePimpl<FRingBufferAllocator>(PendingPageStagingMemory.Num());

	ClusterScatterUpdates = MakePimpl<FOrderedScatterUpdater>(MaxPageInstallsPerUpdate * 128);
	HierarchyScatterUpdates = MakePimpl<FOrderedScatterUpdater>(MaxPageInstallsPerUpdate * 64);


	ResetStreamingStateCPU();

	PageUploader = MakePimpl<FStreamingPageUploader>();

	const bool bReservedResource = (GRHIGlobals.ReservedResources.Supported && GNaniteStreamingReservedResources);

	FRDGBufferDesc ClusterDataBufferDesc = {};
	if (bReservedResource)
	{
		const uint64 MaxSizeInBytes = uint64(GetMaxPagePoolSizeInMB()) << 20;
		ClusterDataBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(MaxSizeInBytes);
		ClusterDataBufferDesc.Usage |= EBufferUsageFlags::ReservedResource;
	}
	else
	{
		ClusterDataBufferDesc = FRDGBufferDesc::CreateByteAddressDesc(4);
	}

	// Keep non-reserved resource allocations grow only for now to avoid repeating expensive resizes
	Hierarchy.Allocator = FSpanAllocator(true);
	ImposterData.Allocator = FSpanAllocator(true);

	if (!bReservedResource || GNaniteStreamingReservedResourceRootPageGrowOnly != 0)
	{
		ClusterPageData.Allocator = FSpanAllocator(true);
	}
		
	ImposterData.DataBuffer = AllocatePooledBufferCurrentLLMTag(RHICmdList, FRDGBufferDesc::CreateByteAddressDesc(4), TEXT("Nanite.StreamingManager.ImposterData"));
	ClusterPageData.DataBuffer = AllocatePooledBufferCurrentLLMTag(RHICmdList, ClusterDataBufferDesc, TEXT("Nanite.StreamingManager.ClusterPageData"));
	Hierarchy.DataBuffer = AllocatePooledBufferCurrentLLMTag(RHICmdList, FRDGBufferDesc::CreateByteAddressDesc(4), TEXT("Nanite.StreamingManager.HierarchyData"));

#if WITH_EDITOR
	RequestOwner = new FRequestOwner(EPriority::Normal);
#endif
}

void FStreamingManager::ResetStreamingStateCPU()
{
	RegisteredVirtualPages.Empty();
	RegisteredVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());

	RegisteredPages.Empty();
	RegisteredPages.SetNum(MaxStreamingPages);

	RegisteredPageDependencies.Empty();
	RegisteredPageDependencies.SetNum(MaxStreamingPages);

	RegisteredPageIndexToLRU.Empty();
	RegisteredPageIndexToLRU.SetNum(MaxStreamingPages);

	LRUToRegisteredPageIndex.Empty();
	LRUToRegisteredPageIndex.SetNum(MaxStreamingPages);
	for (uint32 i = 0; i < MaxStreamingPages; i++)
	{
		RegisteredPageIndexToLRU[i] = i;
		LRUToRegisteredPageIndex[i] = i;
	}

	ResidentPages.Empty();
	ResidentPages.SetNum(MaxStreamingPages);

	for (FFixupChunk* FixupChunk : ResidentPageFixupChunks)
	{
		FMemory::Free( FixupChunk );
	}
	ResidentPageFixupChunks.Empty();
	ResidentPageFixupChunks.SetNum(MaxStreamingPages);

	ResidentVirtualPages.Empty();
	ResidentVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());

	PendingPages.Empty();
	PendingPages.SetNum(MaxPendingPages);

	NumPendingPages = 0;
	NextPendingPageIndex = 0;
	ModifiedResources.Empty();

	PendingPageStagingAllocator->Reset();
}

void FStreamingManager::UpdatePageConfiguration()
{
#if 0	// Stress test resize
	static uint32 ResizeCounter = 0u;
	if (++ResizeCounter % 30u == 0u)
	{
		GNaniteStreamingPoolSize = FMath::RandRange(16, 256);	// crash repro
	}
#endif

	const uint32 MaxPoolSizeInMB = GetMaxPagePoolSizeInMB();
	const uint32 StreamingPoolSizeInMB = GNaniteStreamingPoolSize;
	if (StreamingPoolSizeInMB >= MaxPoolSizeInMB)
	{
		UE_LOG(LogNaniteStreaming, Fatal, TEXT("Streaming pool size (%dMB) must be smaller than the largest allocation supported by the graphics hardware (%dMB)"), StreamingPoolSizeInMB, MaxPoolSizeInMB);
	}

	const uint32 OldMaxStreamingPages = MaxStreamingPages;
	const uint32 OldNumInitialRootPages = NumInitialRootPages;

	const uint64 MaxRootPoolSizeInMB = MaxPoolSizeInMB - StreamingPoolSizeInMB;
	MaxStreamingPages = uint32((uint64(StreamingPoolSizeInMB) << 20) >> NANITE_STREAMING_PAGE_GPU_SIZE_BITS);
	MaxRootPages = uint32((uint64(MaxRootPoolSizeInMB) << 20) >> NANITE_ROOT_PAGE_GPU_SIZE_BITS);

	check(MaxStreamingPages + MaxRootPages <= NANITE_MAX_GPU_PAGES);
	check((MaxStreamingPages << NANITE_STREAMING_PAGE_MAX_CLUSTERS_BITS) + (MaxRootPages << NANITE_ROOT_PAGE_MAX_CLUSTERS_BITS) <= (1u << NANITE_POOL_CLUSTER_REF_BITS));

	NumInitialRootPages = GNaniteStreamingNumInitialRootPages;
	if (NumInitialRootPages > MaxRootPages)
	{
		if(NumInitialRootPages != PrevNumInitialRootPages || MaxStreamingPages != OldMaxStreamingPages)
		{
			UE_LOG(LogNaniteStreaming, Log, TEXT("r.Nanite.Streaming.NumInitialRootPages clamped from %d to %d.\n"
				"Graphics hardware max buffer size: %dMB, Streaming pool size: %dMB, Max root pool size: %" UINT64_FMT "MB (%d pages)."),
				NumInitialRootPages, MaxRootPages,
				MaxPoolSizeInMB, StreamingPoolSizeInMB, MaxRootPoolSizeInMB, MaxRootPages);
		}
		NumInitialRootPages = MaxRootPages;
	}
	PrevNumInitialRootPages = GNaniteStreamingNumInitialRootPages;
}

void FStreamingManager::ReleaseRHI()
{
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

#if WITH_EDITOR
	delete RequestOwner;
	RequestOwner = nullptr;
#endif

	LLM_SCOPE_BYTAG(Nanite);
	for (FFixupChunk* FixupChunk : ResidentPageFixupChunks)
	{
		FMemory::Free(FixupChunk);
	}

	ImposterData.Release();
	ClusterPageData.Release();
	Hierarchy.Release();
	ReadbackManager.Reset();

	PendingPages.Empty();	// Make sure IO handles are released before IO system is shut down

	PageUploader.Reset();
}

void FStreamingManager::Add( FResources* Resources )
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_AddResource);
	check(Resources != nullptr);	// Needed to make static analysis happy
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);

	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	LLM_SCOPE_BYTAG(Nanite);
	if (Resources->RuntimeResourceID == INDEX_NONE)
	{
		check(Resources->RootData.Num() > 0);

		const uint32 NumHierarchyNodes = Resources->HierarchyNodes.Num();
		const uint32 NumHierarchyNodeDwords = NumHierarchyNodes * NANITE_HIERARCHY_NODE_SLICE_SIZE_DWORDS;
		const uint32 NumAssemblyTransformDwords = Resources->AssemblyTransforms.Num() * NANITE_ASSEMBLY_TRANSFORM_SIZE_DWORDS;
		const uint32 NumBoneAttachmentDataDwords = Resources->AssemblyBoneAttachmentData.Num();
		const uint32 TotalHierarchyDwords = NumHierarchyNodeDwords + NumAssemblyTransformDwords + NumBoneAttachmentDataDwords;

		Resources->HierarchyOffset = Hierarchy.Allocator.Allocate(TotalHierarchyDwords);
		Resources->AssemblyTransformOffset = NumAssemblyTransformDwords > 0 ? Resources->HierarchyOffset + NumHierarchyNodeDwords : MAX_uint32;
		Resources->NumHierarchyNodes = NumHierarchyNodes;
		Resources->NumHierarchyDwords = TotalHierarchyDwords;
		Hierarchy.TotalUpload += TotalHierarchyDwords;
		
		StatNumHierarchyNodes += Resources->NumHierarchyNodes;
		StatPeakHierarchyNodes = FMath::Max(StatPeakHierarchyNodes, StatNumHierarchyNodes);

		INC_DWORD_STAT_BY(STAT_NaniteStreaming00_NaniteResources, 1);
		SET_DWORD_STAT(   STAT_NaniteStreaming02_HierarchyNodes, StatNumHierarchyNodes);
		SET_DWORD_STAT(   STAT_NaniteStreaming03_PeakHierarchyNodes, StatPeakHierarchyNodes);
		INC_DWORD_STAT_BY(STAT_NaniteStreaming06_RootPages, Resources->NumRootPages);

		Resources->RootPageIndex = ClusterPageData.Allocator.Allocate( Resources->NumRootPages );
		if (GNaniteStreamingDynamicallyGrowAllocations == 0 && (uint32)ClusterPageData.Allocator.GetMaxSize() > NumInitialRootPages)
		{
			UE_LOG(LogNaniteStreaming, Fatal, TEXT("Out of root pages. Increase the initial root page allocation (r.Nanite.Streaming.NumInitialRootPages) or allow it to grow dynamically (r.Nanite.Streaming.DynamicallyGrowAllocations)."));
		}
		StatNumRootPages += Resources->NumRootPages;

		StatPeakRootPages = FMath::Max(StatPeakRootPages, StatNumRootPages);
		SET_DWORD_STAT(STAT_NaniteStreaming07_PeakRootPages, StatPeakRootPages);


	#if !NANITE_IMPOSTERS_SUPPORTED
		check(Resources->ImposterAtlas.Num() == 0);
	#endif
		if (GNaniteStreamingImposters && Resources->ImposterAtlas.Num())
		{
			Resources->ImposterIndex = ImposterData.Allocator.Allocate(1);
			if (GNaniteStreamingDynamicallyGrowAllocations == 0 && ImposterData.Allocator.GetMaxSize() > GNaniteStreamingNumInitialImposters)
			{
				UE_LOG(LogNaniteStreaming, Fatal, TEXT("Out of imposters. Increase the initial imposter allocation (r.Nanite.Streaming.NumInitialImposters) or allow it to grow dynamically (r.Nanite.Streaming.DynamicallyGrowAllocations)."));
			}
			ImposterData.TotalUpload++;
			INC_DWORD_STAT_BY( STAT_NaniteStreaming01_Imposters, 1 );
		}

		if ((uint32)Resources->RootPageIndex >= MaxRootPages)
		{
			const uint32 MaxPagePoolSize = GetMaxPagePoolSizeInMB();
			UE_LOG(LogNaniteStreaming, Fatal, TEXT(	"Cannot allocate more root pages %d/%d. Pool resource has grown to maximum size of %dMB.\n"
													"%dMB is spent on streaming data, leaving %dMB for %d root pages."),
													MaxRootPages, MaxRootPages, MaxPagePoolSize, GNaniteStreamingPoolSize, MaxPagePoolSize - GNaniteStreamingPoolSize, MaxRootPages);
		}
		RootPageInfos.SetNum(ClusterPageData.Allocator.GetMaxSize());

		RootPageVersions.SetNumZeroed(FMath::Max(RootPageVersions.Num(), ClusterPageData.Allocator.GetMaxSize()));	// Never shrink, so we never forget versions for root slots that were once allocated.
																													// We need this to filter streaming requests that could still be in flight.

		
		const uint32 NumResourcePages = Resources->PageStreamingStates.Num();
		const uint32 VirtualPageRangeStart = VirtualPageAllocator.Allocate(NumResourcePages);

		RegisteredVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());
		ResidentVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());

		INC_DWORD_STAT_BY(STAT_NaniteStreaming0B_TotalStreamingPages, NumResourcePages - Resources->NumRootPages);

		uint32 RuntimeResourceID;
		{
			FRootPageInfo& RootPageInfo = RootPageInfos[Resources->RootPageIndex];
			uint8& RootPageNextVersion = RootPageVersions[Resources->RootPageIndex];
			// Version root pages so we can disregard invalid streaming requests.
			// TODO: We only need enough versions to cover the frame delay from the GPU, so most of the version bits can be reclaimed.
			RuntimeResourceID = (RootPageNextVersion << NANITE_MAX_GPU_PAGES_BITS) | Resources->RootPageIndex;
			RootPageNextVersion = (RootPageNextVersion + 1u) & MAX_RUNTIME_RESOURCE_VERSIONS_MASK;
		}
		Resources->RuntimeResourceID = RuntimeResourceID;

		for (uint32 i = 0; i < Resources->NumRootPages; i++)
		{
			FRootPageInfo& RootPageInfo			= RootPageInfos[Resources->RootPageIndex + i];
			check(RootPageInfo == FRootPageInfo());

			RootPageInfo.Resources				= Resources;
			RootPageInfo.RuntimeResourceID		= RuntimeResourceID;
			RootPageInfo.VirtualPageRangeStart	= VirtualPageRangeStart + i;
			RootPageInfo.NumRootPages			= Resources->NumRootPages;
			RootPageInfo.NumTotalPages			= NumResourcePages;
		}

		if (VerificationLevel() >= 1)
		{
			for (uint32 i = 0; i < NumResourcePages; i++)
			{
				check(RegisteredVirtualPages[VirtualPageRangeStart + i] == FRegisteredVirtualPage());
				check(ResidentVirtualPages[VirtualPageRangeStart + i] == FResidentVirtualPage());
			}
		}

		check(Resources->PersistentHash != NANITE_INVALID_PERSISTENT_HASH);
		PersistentHashResourceMap.Add(Resources->PersistentHash, Resources);
		
		PendingAdds.Add( Resources );
		NumResources++;
	}
}

void FStreamingManager::Remove( FResources* Resources )
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_RemoveResource);
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);

	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	LLM_SCOPE_BYTAG(Nanite);
	if (Resources->RuntimeResourceID != INDEX_NONE)
	{
		Hierarchy.Allocator.Free( Resources->HierarchyOffset, Resources->NumHierarchyDwords );
		Resources->HierarchyOffset = INDEX_NONE;

		const uint32 RootPageIndex = Resources->RootPageIndex;
		const uint32 NumRootPages = Resources->NumRootPages;
		ClusterPageData.Allocator.Free( RootPageIndex, NumRootPages);
		Resources->RootPageIndex = INDEX_NONE;

		if (Resources->ImposterIndex != INDEX_NONE)
		{
			ImposterData.Allocator.Free( Resources->ImposterIndex, 1 );
			Resources->ImposterIndex = INDEX_NONE;
			DEC_DWORD_STAT_BY( STAT_NaniteStreaming01_Imposters, 1 );
		}

		StatNumHierarchyNodes -= Resources->NumHierarchyNodes;

		const uint32 NumResourcePages = Resources->PageStreamingStates.Num();
		DEC_DWORD_STAT_BY(STAT_NaniteStreaming0B_TotalStreamingPages, NumResourcePages - NumRootPages);
		DEC_DWORD_STAT_BY(STAT_NaniteStreaming00_NaniteResources, 1);
		SET_DWORD_STAT(   STAT_NaniteStreaming02_HierarchyNodes, StatNumHierarchyNodes);
		DEC_DWORD_STAT_BY(STAT_NaniteStreaming06_RootPages, NumRootPages);
		
		StatNumRootPages -= NumRootPages;
		
		const uint32 VirtualPageRangeStart = RootPageInfos[RootPageIndex].VirtualPageRangeStart;
		
		// Move all registered pages to the free list. No need to properly uninstall them as they are no longer referenced from the hierarchy.
		for( uint32 PageIndex = NumRootPages; PageIndex < NumResourcePages; PageIndex++ )
		{
			const uint32 VirtualPageIndex = VirtualPageRangeStart + PageIndex;
			const uint32 RegisteredPageIndex = RegisteredVirtualPages[VirtualPageIndex].RegisteredPageIndex;
			if(RegisteredPageIndex != INDEX_NONE)
			{
				RegisteredPages[RegisteredPageIndex] = FRegisteredPage();
				RegisteredPageDependencies[RegisteredPageIndex].Reset();
			}
			RegisteredVirtualPages[VirtualPageIndex] = FRegisteredVirtualPage();

			const uint32 ResidentPageIndex = ResidentVirtualPages[VirtualPageIndex].ResidentPageIndex;
			if (ResidentPageIndex != INVALID_RESIDENT_PAGE_INDEX)
			{
				UninstallResidentPage(ResidentPageIndex, MaxStreamingPages, nullptr, false);
				check(ResidentVirtualPages[VirtualPageIndex] == FResidentVirtualPage());
			}
		}

		for (uint32 i = 0; i < NumRootPages; i++)
		{
			FRootPageInfo& RootPageInfo = RootPageInfos[RootPageIndex + i];
			FMemory::Free(RootPageInfo.FixupChunk);

			if (RootPageInfo.MaxHierarchyDepth != 0xFFu)
			{
				HierarchyDepthManager->Remove(RootPageInfo.MaxHierarchyDepth);
			}

			RootPageInfo = FRootPageInfo();
		}

		VirtualPageAllocator.Free(VirtualPageRangeStart, NumResourcePages);

		Resources->RuntimeResourceID = INDEX_NONE;

		check(Resources->PersistentHash != NANITE_INVALID_PERSISTENT_HASH);
		int32 NumRemoved = PersistentHashResourceMap.Remove(Resources->PersistentHash, Resources);
		check(NumRemoved == 1);
		Resources->PersistentHash = NANITE_INVALID_PERSISTENT_HASH;
		
		PendingAdds.Remove( Resources );
		NumResources--;
	}
}

FResources* FStreamingManager::GetResources(uint32 RuntimeResourceID)
{
	if (RuntimeResourceID != INDEX_NONE)
	{
		const uint32 RootPageIndex = RuntimeResourceID & NANITE_MAX_GPU_PAGES_MASK;
		if (RootPageIndex < (uint32)RootPageInfos.Num())
		{
			FRootPageInfo& RootPageInfo = RootPageInfos[RootPageIndex];
			if (RootPageInfo.RuntimeResourceID == RuntimeResourceID)
			{
				return RootPageInfo.Resources;
			}
		}
	}
	return nullptr;
}

FStreamingManager::FRootPageInfo* FStreamingManager::GetRootPage(uint32 RuntimeResourceID)
{
	if (RuntimeResourceID != INDEX_NONE)
	{
		const uint32 RootPageIndex = RuntimeResourceID & NANITE_MAX_GPU_PAGES_MASK;
		if (RootPageIndex < (uint32)RootPageInfos.Num())
		{
			FRootPageInfo& RootPageInfo = RootPageInfos.GetData()[RootPageIndex];
			if (RootPageInfo.RuntimeResourceID == RuntimeResourceID)
			{
				return &RootPageInfo;
			}
		}
	}
	return nullptr;
}

FRDGBuffer* FStreamingManager::GetStreamingRequestsBuffer(FRDGBuilder& GraphBuilder) const
{
	return ReadbackManager->GetStreamingRequestsBuffer(GraphBuilder);
}

FRDGBufferSRV* FStreamingManager::GetHierarchySRV(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Hierarchy.DataBuffer));
}

FRDGBufferSRV* FStreamingManager::GetClusterPageDataSRV(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(ClusterPageData.DataBuffer));
}

FRDGBufferSRV* FStreamingManager::GetImposterDataSRV(FRDGBuilder& GraphBuilder) const
{
	return GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(ImposterData.DataBuffer));
}

void FStreamingManager::RegisterStreamingPage(uint32 RegisteredPageIndex, const FPageKey& Key)
{
	LLM_SCOPE_BYTAG(Nanite);

	FResources* Resources = GetResources( Key.RuntimeResourceID );
	check( Resources != nullptr );
	check( !Resources->IsRootPage(Key.PageIndex) );
	
	TArray< FPageStreamingState >& PageStreamingStates = Resources->PageStreamingStates;
	FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	
	const uint32 VirtualPageRangeStart = RootPageInfos[Resources->RootPageIndex].VirtualPageRangeStart;

	FRegisteredPageDependencies& Dependencies = RegisteredPageDependencies[RegisteredPageIndex];
	Dependencies.Reset();

	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( Resources->IsRootPage( DependencyPageIndex ) )
			continue;

		const uint32 DependencyVirtualPageIndex = VirtualPageRangeStart + DependencyPageIndex;
		const uint32 DependencyRegisteredPageIndex = RegisteredVirtualPages[DependencyVirtualPageIndex].RegisteredPageIndex;
		check(DependencyRegisteredPageIndex != INDEX_NONE);
		
		FRegisteredPage& DependencyPage = RegisteredPages[DependencyRegisteredPageIndex];
		check(DependencyPage.RefCount != 0xFF);
		DependencyPage.RefCount++;
		Dependencies.Add(VirtualPageRangeStart + DependencyPageIndex);
	}
	
	FRegisteredPage& RegisteredPage = RegisteredPages[RegisteredPageIndex];
	RegisteredPage = FRegisteredPage();
	RegisteredPage.Key = Key;
	RegisteredPage.VirtualPageIndex = VirtualPageRangeStart + Key.PageIndex;
	
	RegisteredVirtualPages[RegisteredPage.VirtualPageIndex].RegisteredPageIndex = RegisteredPageIndex;
	MoveToEndOfLRUList(RegisteredPageIndex);
}

void FStreamingManager::UnregisterStreamingPage( const FPageKey& Key )
{
	LLM_SCOPE_BYTAG(Nanite);
	
	if( Key.RuntimeResourceID == INDEX_NONE)
	{
		return;
	}

	const FRootPageInfo* RootPage = GetRootPage(Key.RuntimeResourceID);
	check(RootPage);
	const FResources* Resources = RootPage->Resources;
	check( Resources != nullptr );
	check( !Resources->IsRootPage(Key.PageIndex) );

	const uint32 VirtualPageRangeStart = RootPage->VirtualPageRangeStart;

	const uint32 RegisteredPageIndex = RegisteredVirtualPages[VirtualPageRangeStart + Key.PageIndex].RegisteredPageIndex;
	check(RegisteredPageIndex != INDEX_NONE);
	FRegisteredPage& RegisteredPage = RegisteredPages[RegisteredPageIndex];

	// Decrement reference counts of dependencies.
	const TArray< FPageStreamingState >& PageStreamingStates = Resources->PageStreamingStates;
	const FPageStreamingState& PageStreamingState = PageStreamingStates[ Key.PageIndex ];
	for( uint32 i = 0; i < PageStreamingState.DependenciesNum; i++ )
	{
		const uint32 DependencyPageIndex = Resources->PageDependencies[ PageStreamingState.DependenciesStart + i ];
		if( Resources->IsRootPage( DependencyPageIndex ) )
			continue;

		const uint32 DependencyRegisteredPageIndex = RegisteredVirtualPages[VirtualPageRangeStart + DependencyPageIndex].RegisteredPageIndex;
		RegisteredPages[DependencyRegisteredPageIndex].RefCount--;
	}
	check(RegisteredPage.RefCount == 0);

	RegisteredVirtualPages[RegisteredPage.VirtualPageIndex] = FRegisteredVirtualPage();
	RegisteredPage = FRegisteredPage();
	RegisteredPageDependencies[RegisteredPageIndex].Reset();
}

bool FStreamingManager::ArePageDependenciesCommitted(const FResources& Resources, FPageRangeKey PageRangeKey, uint32 PageToExclude, uint32 VirtualPageRangeStart)
{
	const uint32 RuntimeResourceID = Resources.RuntimeResourceID;
	const bool bAllCommitted = Resources.TrueForAllPages(
		PageRangeKey,
		[this, PageToExclude, RuntimeResourceID, VirtualPageRangeStart](uint32 PageIndex)
		{
			if (PageIndex == PageToExclude)
			{
				return false;
			}

			const uint32 ResidentPageIndex = ResidentVirtualPages[VirtualPageRangeStart + PageIndex].ResidentPageIndex;
			if (ResidentPageIndex != INVALID_RESIDENT_PAGE_INDEX)
			{
				check(ResidentPages[ResidentPageIndex].Key == FPageKey(RuntimeResourceID, PageIndex))
				return true;
			}

			return false;
		},
		true // bStreamingPagesOnly
	);

	return bAllCommitted;
}

static uint32 GPUPageIndexToGPUOffset(uint32 MaxStreamingPages, uint32 PageIndex)
{
	return (FMath::Min(PageIndex, MaxStreamingPages) << NANITE_STREAMING_PAGE_GPU_SIZE_BITS) + ((uint32)FMath::Max((int32)PageIndex - (int32)MaxStreamingPages, 0) << NANITE_ROOT_PAGE_GPU_SIZE_BITS);
}

static const TCHAR* GetNaniteResourceName(const FResources& Resources)
{
#if WITH_EDITOR
	return *Resources.ResourceName;
#else
	return TEXT("Unknown");
#endif
}

static bool VerifyFixupChunk(const FFixupChunk& FixupChunk, const FResources& Resources, bool bFatal)
{
	const bool bValid =	FixupChunk.Header.Magic == NANITE_FIXUP_MAGIC;
	if (!bValid)
	{
		if (bFatal)
		{
			UE_LOG(LogNaniteStreaming, Fatal,
				TEXT("Encountered a corrupt fixup chunk for resource (%s). Magic: %4X. This should never happen."),
				GetNaniteResourceName(Resources),
				FixupChunk.Header.Magic
			);
		}
		else
		{
			UE_LOG(LogNaniteStreaming, Error,
				TEXT("Encountered a corrupt fixup chunk for resource (%s). Magic: %4X. This should never happen."),
				GetNaniteResourceName(Resources),
				FixupChunk.Header.Magic
			);
		}
		
	}
	return bValid;
}

// Applies the fixups required to install/uninstall a page.
// Hierarchy references are patched up and leaf flags of parent clusters are set accordingly.
void FStreamingManager::ApplyFixups( const FFixupChunk& FixupChunk, const FResources& Resources, const TSet<uint32>* NoWriteGPUPages, uint32 NumStreamingPages, uint32 PageToExclude, uint32 VirtualPageRangeStart, bool bUninstall, bool bAllowReconsider, bool bAllowReinstall )
{
	LLM_SCOPE_BYTAG(Nanite);
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_ApplyFixup);

	VerifyFixupChunk(FixupChunk, Resources, true);

	const uint32 RuntimeResourceID = Resources.RuntimeResourceID;
	const uint32 HierarchyOffset = Resources.HierarchyOffset;

	for (uint32 i = 0; i < FixupChunk.Header.NumGroupFixups; i++)
	{
		FFixupChunk::FGroupFixup& GroupFixup = FixupChunk.GetGroupFixup(i);

		if (!bAllowReinstall && bUninstall == ((GroupFixup.Flags & NANITE_FIXUP_FLAG_INSTALLED) == 0u))
			continue;

		const bool bPageDependenciesSatisfied = ArePageDependenciesCommitted(Resources, GroupFixup.PageDependencies, PageToExclude, VirtualPageRangeStart);
		
		if (bUninstall == bPageDependenciesSatisfied)
			continue;

		if (bUninstall)
		{
			GroupFixup.Flags &= ~NANITE_FIXUP_FLAG_INSTALLED;
		}
		else
		{
			GroupFixup.Flags |= NANITE_FIXUP_FLAG_INSTALLED;
		}

		for (uint32 j = 0; j < GroupFixup.NumPartFixups; j++)
		{
			const FFixupChunk::FPartFixup& PartFixup = FixupChunk.GetPartFixup(GroupFixup.FirstPartFixup + j);

			// Install page to hierarchy
			FPageKey TargetKey = { RuntimeResourceID, PartFixup.PageIndex };
			uint32 TargetGPUPageIndex = INDEX_NONE;
			if (!bUninstall)
			{
				if (Resources.IsRootPage(TargetKey.PageIndex))
				{
					TargetGPUPageIndex = NumStreamingPages + Resources.RootPageIndex + TargetKey.PageIndex;
				}
				else
				{
					TargetGPUPageIndex = ResidentVirtualPages[VirtualPageRangeStart + TargetKey.PageIndex].ResidentPageIndex;
					check(TargetGPUPageIndex != INVALID_RESIDENT_PAGE_INDEX);
					const FResidentPage& TargetPage = ResidentPages[TargetGPUPageIndex];
					check(TargetPage.Key == TargetKey);
				}
			}

			for (uint32 k = 0; k < PartFixup.NumHierarchyLocations; k++)
			{
				const FFixupChunk::FHierarchyLocation& HierarchyLocation = FixupChunk.GetHierarchyLocation(PartFixup.FirstHierarchyLocation + k);

				const uint32 HierarchyNodeIndex = HierarchyLocation.GetNodeIndex();
				check(HierarchyNodeIndex < Resources.NumHierarchyNodes);
				const uint32 ChildIndex = HierarchyLocation.GetChildIndex();
				const uint32 ChildStartReference = bUninstall ? 0xFFFFFFFFu : ((TargetGPUPageIndex << NANITE_MAX_CLUSTERS_PER_PAGE_BITS) | PartFixup.StartClusterIndex);
				const uint32 Offset = (size_t)&(((FPackedHierarchyNode*)nullptr)[HierarchyNodeIndex].Misc1[ChildIndex].ChildStartReference);	//TODO: Maybe we should just bake out this offset?

				HierarchyScatterUpdates->Add(EScatterOp::Write, HierarchyOffset * 4 + Offset, ChildStartReference);
			}
		}

		for (uint32 j = 0; j < GroupFixup.NumParentFixups; j++)
		{
			const FFixupChunk::FParentFixup& ParentFixup = FixupChunk.GetParentFixup(GroupFixup.FirstParentFixup + j);

			// Update hierarchy MinLOD state
			{
				const FPageKey PartFixupKey = { RuntimeResourceID, ParentFixup.PartFixupPageIndex };

				FFixupChunk* ParentFixupChunk;
				if (Resources.IsRootPage(PartFixupKey.PageIndex))
				{
					const uint32 GPUPageIndex = NumStreamingPages + Resources.RootPageIndex + PartFixupKey.PageIndex;
					ParentFixupChunk = RootPageInfos[Resources.RootPageIndex + PartFixupKey.PageIndex].FixupChunk;
				}
				else
				{
					const uint32 GPUPageIndex = ResidentVirtualPages[VirtualPageRangeStart + PartFixupKey.PageIndex].ResidentPageIndex;
					check(GPUPageIndex != INVALID_RESIDENT_PAGE_INDEX);
					ParentFixupChunk = ResidentPageFixupChunks[GPUPageIndex];
				}

				FFixupChunk::FPartFixup& ParentPartFixup = ParentFixupChunk->GetPartFixup(ParentFixup.PartFixupIndex);
				uint8& LeafCounter = ParentPartFixup.LeafCounter;

				// Parent hierarchy fixup
				const uint8 OldLeafCounter = LeafCounter;
				if (bUninstall)
				{
					check(LeafCounter != 0xFF);
					LeafCounter++;
				}
				else
				{
					check(LeafCounter != 0);
					LeafCounter--;
				}
				
				if (LeafCounter == 0 || OldLeafCounter == 0)
				{
					for (uint32 k = 0; k < ParentPartFixup.NumHierarchyLocations; k++)
					{
						const FFixupChunk::FHierarchyLocation& HierarchyLocation = ParentFixupChunk->GetHierarchyLocation(ParentPartFixup.FirstHierarchyLocation + k);

						const uint32 HierarchyNodeIndex = HierarchyLocation.GetNodeIndex();
						check(HierarchyNodeIndex < Resources.NumHierarchyNodes);
						const uint32 ChildIndex = HierarchyLocation.GetChildIndex();
						const uint32 Offset = HierarchyOffset * 4 + (size_t) & (((FPackedHierarchyNode*)nullptr)[HierarchyNodeIndex].Misc0[ChildIndex].MinLODError_MaxParentLODError);	//TODO: Maybe we should just bake out this offset?

						if (LeafCounter == 0)
						{
							check(OldLeafCounter > 0);
							// Clear negative bit from MinLODError
							HierarchyScatterUpdates->Add(EScatterOp::And, Offset, 0x7FFFFFFFu);
						}
						else
						{
							check(OldLeafCounter == 0);
							check(LeafCounter > 0);
							// Set negative bit of MinLODError
							HierarchyScatterUpdates->Add(EScatterOp::Or, Offset, 0x80000000u);
						}
					}
				}
			}

			// Parent leaf fixup
			{
				const FPageKey TargetKey = { RuntimeResourceID, ParentFixup.PageIndex };

				uint32 TargetGPUPageIndex;
				FFixupChunk* TargetFixupChunk;
				if (Resources.IsRootPage(TargetKey.PageIndex))
				{
					TargetGPUPageIndex = NumStreamingPages + Resources.RootPageIndex + TargetKey.PageIndex;
					TargetFixupChunk = RootPageInfos[Resources.RootPageIndex + TargetKey.PageIndex].FixupChunk;
				}
				else
				{
					TargetGPUPageIndex = ResidentVirtualPages[VirtualPageRangeStart + TargetKey.PageIndex].ResidentPageIndex;
					TargetFixupChunk = ResidentPageFixupChunks[TargetGPUPageIndex];
					check(ResidentPages[TargetGPUPageIndex].Key == TargetKey);
				}

				if (!NoWriteGPUPages || !NoWriteGPUPages->Contains(TargetGPUPageIndex))
				{
					const uint32 NumTargetPageClusters = TargetFixupChunk->Header.NumClusters;
					for (uint32 k = 0; k < ParentFixup.NumClusterIndices; k++)
					{
						const uint32 ClusterIndex = FixupChunk.GetClusterIndex(ParentFixup.FirstClusterIndex + k);
						check(ClusterIndex < NumTargetPageClusters);

						const uint32 FlagsOffset = offsetof(FPackedCluster, Flags_NumClusterBoneInfluences);
						const uint32 Offset = GPUPageIndexToGPUOffset(NumStreamingPages, TargetGPUPageIndex) + NANITE_GPU_PAGE_HEADER_SIZE + ((FlagsOffset >> 4) * NumTargetPageClusters + ClusterIndex) * 16 + (FlagsOffset & 15);
						check((Offset & 3u) == 0);

						if (bUninstall)
						{
							ClusterScatterUpdates->Add(EScatterOp::Or, Offset, NANITE_CLUSTER_FLAG_STREAMING_LEAF);
						}
						else
						{
							ClusterScatterUpdates->Add(EScatterOp::And, Offset, ~NANITE_CLUSTER_FLAG_STREAMING_LEAF);
						}
					}
				}
			}
		}
	}

	// Reconsider other pages
	for (uint32 i = 0; i < FixupChunk.Header.NumReconsiderPages; i++)
	{
		const uint32 ReconsiderPageIndex = ResidentVirtualPages[VirtualPageRangeStart + FixupChunk.GetReconsiderPageIndex(i)].ResidentPageIndex;
		if (ReconsiderPageIndex != INVALID_RESIDENT_PAGE_INDEX)
		{
			ApplyFixups(*ResidentPageFixupChunks[ReconsiderPageIndex], Resources, NoWriteGPUPages, NumStreamingPages, PageToExclude, VirtualPageRangeStart, bUninstall, false, false);
		}
	}
}

void FStreamingManager::VerifyFixupState()
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_VerifyFixupState);
	for (uint32 GPUPageIndex = 0; GPUPageIndex < (uint32)ResidentPages.Num(); GPUPageIndex++)
	{
		const FResidentPage& ResidentPage = ResidentPages[GPUPageIndex];
		if (ResidentPage.Key.RuntimeResourceID != INDEX_NONE)
		{
			const FRootPageInfo* RootPageInfo = GetRootPage(ResidentPage.Key.RuntimeResourceID);
			check(RootPageInfo);

			const FFixupChunk& FixupChunk = *ResidentPageFixupChunks[GPUPageIndex];
			const uint32 VirtualPageRangeStart = RootPageInfo->VirtualPageRangeStart;
			const FResources& Resources = *RootPageInfo->Resources;

			for (uint32 i = 0; i < FixupChunk.Header.NumGroupFixups; i++)
			{
				const FFixupChunk::FGroupFixup& GroupFixup = FixupChunk.GetGroupFixup(i);

				const bool bInstalled = ((GroupFixup.Flags & NANITE_FIXUP_FLAG_INSTALLED) != 0u);
				const bool bSatisfied = ArePageDependenciesCommitted(Resources, GroupFixup.PageDependencies, MAX_uint32, VirtualPageRangeStart);
					
				if (bInstalled != bSatisfied)
				{
					FString PageDependenciesStr;
					Resources.ForEachPage(
						GroupFixup.PageDependencies,
						[&PageDependenciesStr](uint32 PageIndex)
						{
							PageDependenciesStr += FString::FromInt(PageIndex) + TEXT(", ");
						},
						true);

					UE_LOG(LogNaniteStreaming, Verbose, TEXT("FixupVerifyState failed for page GPUpage %u: Key: (%x, %u) Group: %u, Installed: %u, Satisfied: %u, PageDependencies: %s"),
														GPUPageIndex, ResidentPage.Key.RuntimeResourceID, ResidentPage.Key.PageIndex, i,
														uint32(bInstalled), uint32(bSatisfied), *PageDependenciesStr);
						
					check(bInstalled == bSatisfied);
				}
			}
		}
	}
}

void FStreamingManager::UninstallResidentPage(uint32 GPUPageIndex, uint32 NumStreamingPages, const TSet<uint32>* NoWriteGPUPages, bool bApplyFixup)
{
	FResidentPage& ResidentPage = ResidentPages[GPUPageIndex];

	// Uninstall GPU page
	if (ResidentPage.Key.RuntimeResourceID != INDEX_NONE)
	{
		// Apply fixups to uninstall page. No need to fix up anything if resource is gone.
		FRootPageInfo* RootPageInfo = GetRootPage(ResidentPage.Key.RuntimeResourceID);
		check(RootPageInfo);

		FResources& Resources = *RootPageInfo->Resources;

		if (bApplyFixup)
		{
			ApplyFixups(*ResidentPageFixupChunks[GPUPageIndex], Resources, NoWriteGPUPages, NumStreamingPages, ResidentPage.Key.PageIndex, RootPageInfo->VirtualPageRangeStart, true, true, false);
		}

		Resources.NumResidentClusters -= ResidentPageFixupChunks[GPUPageIndex]->Header.NumClusters;
		check(Resources.NumResidentClusters > 0);
		check(Resources.NumResidentClusters <= Resources.NumClusters);
		ModifiedResources.Add(ResidentPage.Key.RuntimeResourceID, Resources.NumResidentClusters);

		if (ResidentPageFixupChunks[GPUPageIndex]->GetSize() <= 1024)
		{
			// Reuse the allocation later, but mark it, so we are sure to catch if this stale data ends up being used by accident.
			ResidentPageFixupChunks[GPUPageIndex]->Header.Magic = 0xDEAD;
		}
		else
		{
			// Free unusually large fixup allocations
			FMemory::Free(ResidentPageFixupChunks[GPUPageIndex]);
			ResidentPageFixupChunks[GPUPageIndex] = nullptr;
		}

		HierarchyDepthManager->Remove(ResidentPage.MaxHierarchyDepth);

		ResidentVirtualPages[RootPageInfo->VirtualPageRangeStart + ResidentPage.Key.PageIndex].ResidentPageIndex = INVALID_RESIDENT_PAGE_INDEX;
	}

	ResidentPage.Key.RuntimeResourceID = INDEX_NONE;
}

void FStreamingManager::UninstallAllResidentPages(uint32 NumStreamingPages)
{
	// Do it in dependency order, so we can just use the ordinary UninstallResidentPage function,
	// instead of having to maintain custom logic.

	// Set all streaming pages as NoWrite to prevent unnecessary writes pages we are never going to use again.
	TSet<uint32> NoWriteGPUPages;
	NoWriteGPUPages.Reserve(NumStreamingPages);
	for (uint32 PageIndex = 0; PageIndex < NumStreamingPages; PageIndex++)
	{
		NoWriteGPUPages.Add(PageIndex);
	}

	TArray<uint32> DependencyCounters;
	TArray<uint32> NewDependencyCounters;

	// Repeatedly uninstall pages with no dependents until none are left
	bool bFirstIteration = true;
	uint32 NumRemaining;
	do
	{
		NewDependencyCounters.Init(0u, NumStreamingPages);
		NumRemaining = 0u;

		for (uint32 GPUPageIndex = 0; GPUPageIndex < NumStreamingPages; GPUPageIndex++)
		{
			const FResidentPage& ResidentPage = ResidentPages[GPUPageIndex];
			if (ResidentPage.Key.RuntimeResourceID != INDEX_NONE)
			{
				if (!bFirstIteration && DependencyCounters[GPUPageIndex] == 0)
				{
					UninstallResidentPage(GPUPageIndex, NumStreamingPages, &NoWriteGPUPages, true);
				}
				else
				{
					const FRootPageInfo* RootPageInfo = GetRootPage(ResidentPage.Key.RuntimeResourceID);
					check(RootPageInfo);

					const FResources& Resources = *RootPageInfo->Resources;
					const uint32 VirtualPageRangeStart = RootPageInfo->VirtualPageRangeStart;

					const FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[ResidentPage.Key.PageIndex];
					for (uint32 i = 0; i < PageStreamingState.DependenciesNum; i++)
					{
						const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + i];
						check(DependencyPageIndex != ResidentPage.Key.PageIndex);

						if(!Resources.IsRootPage(DependencyPageIndex))
						{
							const uint32 DependencyGPUPageIndex = ResidentVirtualPages[VirtualPageRangeStart + DependencyPageIndex].ResidentPageIndex;
							check(DependencyGPUPageIndex != INVALID_RESIDENT_PAGE_INDEX);
							NewDependencyCounters[DependencyGPUPageIndex]++;
						}
					}
					NumRemaining++;
				}
			}
		}

		Swap(DependencyCounters, NewDependencyCounters);
		bFirstIteration = false;
	} while (NumRemaining > 0u);
}

void FStreamingManager::InstallReadyPages( uint32 NumReadyOrSkippedPages )
{
	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::InstallReadyPages);
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_InstallReadyPages);

	if (NumReadyOrSkippedPages == 0)
		return;

	UE_LOG(LogNaniteStreaming, Verbose, TEXT("InstallReadyPages: %u"), NumReadyOrSkippedPages);

	const uint32 StartPendingPageIndex = ( NextPendingPageIndex + MaxPendingPages - NumPendingPages ) % MaxPendingPages;

	struct FUploadTask
	{
		FPendingPage* PendingPage = nullptr;
		uint8* Dst = nullptr;
		const uint8* Src = nullptr;
		uint32 SrcSize = 0;
	};

#if WITH_EDITOR
	TMap<FResources*, const uint8*> ResourceToBulkPointer;
#endif

	TArray<FUploadTask> UploadTasks;
	UploadTasks.Reserve(NumReadyOrSkippedPages);

	// Install ready pages
	// To make fixup handling simpler, installs and uninstalls are always executed serially on the CPU.
	// FOrderedScatterUpdater guarantees that even when multiple updates are made to the same address,
	// they are resolved as if they were executed serially.
	
	// Keep track of when a GPU page is uploaded to for the last time.
	// Forbid any writes to the page until that write has happened.
	TMap<uint32, uint32> GPUPageToLastPendingPageIndex;
	TSet<uint32> NoWriteGPUPages;	// Ignore writes to GPU pages before they are written
	for (uint32 i = 0; i < NumReadyOrSkippedPages; i++)
	{
		const uint32 PendingPageIndex = (StartPendingPageIndex + i) % MaxPendingPages;
		const FPendingPage& PendingPage = PendingPages[PendingPageIndex];

		const FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
		if (!Resources)
		{
			continue;	// Resource no longer exists. Skip resource install.
		}
			
		
		GPUPageToLastPendingPageIndex.Add(PendingPage.GPUPageIndex, PendingPageIndex);
		NoWriteGPUPages.Add(PendingPage.GPUPageIndex);
	}
	
	// Install pages
	// Must be processed in PendingPages order so FFixupChunks are loaded when we need them.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(InstallReadyPages);
		uint32 NumInstalledPages = 0;
		for (uint32 LocalPageIndex = 0; LocalPageIndex < NumReadyOrSkippedPages; LocalPageIndex++)
		{
			const uint32 PendingPageIndex = (StartPendingPageIndex + LocalPageIndex) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[PendingPageIndex];

			FResidentPage& ResidentPage = ResidentPages[PendingPage.GPUPageIndex];
			UE_LOG(LogNaniteStreaming, Verbose, TEXT("Install GPUPage: %u, InstallKey: (%x, %u), ResidentKey: (%x, %u)"),
												PendingPage.GPUPageIndex, PendingPage.InstallKey.RuntimeResourceID, PendingPage.InstallKey.PageIndex,
												ResidentPage.Key.RuntimeResourceID, ResidentPage.Key.PageIndex);

			const uint32 PageOffset = GPUPageIndexToGPUOffset(MaxStreamingPages, PendingPage.GPUPageIndex);

			UninstallResidentPage(PendingPage.GPUPageIndex, MaxStreamingPages, &NoWriteGPUPages, true);

			FRootPageInfo* RootPageInfo = GetRootPage(PendingPage.InstallKey.RuntimeResourceID);
			if (!RootPageInfo)
			{
				UE_LOG(LogNaniteStreaming, Verbose, TEXT("Skip install. Resource no longer exists."));
				continue;	// Resource no longer exists. Skip resource install.
			}

			if (RootPageInfo->bInvalidResource)
			{
				UE_LOG(LogNaniteStreaming, Verbose, TEXT("Skip install. Resource is marked invalid."));
				continue;
			}

			FResources& Resources = *RootPageInfo->Resources;

			const TArray< FPageStreamingState >& PageStreamingStates = Resources.PageStreamingStates;
			const FPageStreamingState& PageStreamingState = PageStreamingStates[ PendingPage.InstallKey.PageIndex ];

			const uint8* SrcPtr;
		#if WITH_EDITOR
			if(PendingPage.State == FPendingPage::EState::DDC_Ready)
			{
				check(Resources.ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC);
				SrcPtr = (const uint8*)PendingPage.SharedBuffer.GetData();
			}
			else if(PendingPage.State == FPendingPage::EState::Memory)
			{
				// Make sure we only lock each resource BulkData once.
				const uint8* BulkDataPtr = ResourceToBulkPointer.FindRef(&Resources);
				if (BulkDataPtr)
				{
					SrcPtr = BulkDataPtr + PageStreamingState.BulkOffset;
				}
				else
				{
					FByteBulkData& BulkData = Resources.StreamablePages;
					check(BulkData.IsBulkDataLoaded() && BulkData.GetBulkDataSize() > 0);
					BulkDataPtr = (const uint8*)BulkData.LockReadOnly();
					ResourceToBulkPointer.Add(&Resources, BulkDataPtr);
					SrcPtr = BulkDataPtr + PageStreamingState.BulkOffset;
				}
			}
			else
		#endif
			{
		#if WITH_EDITOR
				check(PendingPage.State == FPendingPage::EState::Disk);
		#endif
				SrcPtr = PendingPage.RequestBuffer.GetData();
			}

			if (SrcPtr == nullptr || !VerifyFixupChunk(*(const FFixupChunk*)SrcPtr, Resources, false))
			{
				UE_LOG(LogNaniteStreaming, Verbose, TEXT("Skip install. FixupChunk is invalid. Marking resource as invalid."));
				RootPageInfo->bInvalidResource = true;
				continue;
			}

			const uint32 VirtualPageRangeStart = RootPageInfo->VirtualPageRangeStart;
			ResidentVirtualPages[VirtualPageRangeStart + PendingPage.InstallKey.PageIndex].ResidentPageIndex = PendingPage.GPUPageIndex;

			const uint32 FixupChunkSize = ((const FFixupChunk*)SrcPtr)->GetSize();
			FFixupChunk* FixupChunk = (FFixupChunk*)FMemory::Realloc(ResidentPageFixupChunks[PendingPage.GPUPageIndex], FixupChunkSize, sizeof(uint16));	// TODO: Get rid of this alloc. Can we come up with a tight conservative bound, so we could preallocate?
			ResidentPageFixupChunks[PendingPage.GPUPageIndex] = FixupChunk;
			ResidentPage.MaxHierarchyDepth = PageStreamingState.MaxHierarchyDepth;
			HierarchyDepthManager->Add(ResidentPage.MaxHierarchyDepth);

			FMemory::Memcpy(FixupChunk, SrcPtr, FixupChunkSize);

			Resources.NumResidentClusters += FixupChunk->Header.NumClusters;
			check(Resources.NumResidentClusters > 0);
			check(Resources.NumResidentClusters <= Resources.NumClusters);
			ModifiedResources.Add(PendingPage.InstallKey.RuntimeResourceID, Resources.NumResidentClusters);

			// Build list of GPU page dependencies
			GPUPageDependencies.Reset();
			if(PageStreamingState.Flags & NANITE_PAGE_FLAG_RELATIVE_ENCODING)
			{
				for (uint32 i = 0; i < PageStreamingState.DependenciesNum; i++)
				{
					const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + i];
					if (Resources.IsRootPage(DependencyPageIndex))
					{
						GPUPageDependencies.Add(MaxStreamingPages + Resources.RootPageIndex + DependencyPageIndex);
					}
					else
					{
						const FPageKey DependencyKey = { PendingPage.InstallKey.RuntimeResourceID, DependencyPageIndex };
						const uint32 DependencyStreamingPageIndex = ResidentVirtualPages[VirtualPageRangeStart + DependencyPageIndex].ResidentPageIndex;
						check(DependencyStreamingPageIndex != INVALID_RESIDENT_PAGE_INDEX);
						GPUPageDependencies.Add(DependencyStreamingPageIndex);
					}
				}
			}
			
			const uint32 DataSize = PageStreamingState.BulkSize - FixupChunkSize;
			check(NumInstalledPages < MaxPageInstallsPerUpdate);

			const uint32 LastPendingPageIndex = GPUPageToLastPendingPageIndex.FindChecked(PendingPages[PendingPageIndex].GPUPageIndex);
			if (PendingPageIndex == LastPendingPageIndex)	// Avoid GPU upload race in the rare case where a page is written multiple times in an update
			{
				const FPageKey GPUPageKey = FPageKey{ PendingPage.InstallKey.RuntimeResourceID, PendingPage.GPUPageIndex };

				FUploadTask& UploadTask = UploadTasks.AddDefaulted_GetRef();
				UploadTask.PendingPage = &PendingPage;
				UploadTask.Dst = PageUploader->Add_GetRef(DataSize, FixupChunk->Header.NumClusters, PageOffset, GPUPageKey, GPUPageDependencies);
				UploadTask.Src = SrcPtr + FixupChunkSize;
				UploadTask.SrcSize = DataSize;
				NumInstalledPages++;

				NoWriteGPUPages.Remove(PendingPage.GPUPageIndex);
			}
			else
			{
				UE_LOG(LogNaniteStreaming, Verbose, TEXT("Skip upload."));
			}

			// Apply fixups to install page
			ResidentPage.Key = PendingPage.InstallKey;
			ApplyFixups( *FixupChunk, Resources, &NoWriteGPUPages, MaxStreamingPages, MAX_uint32, VirtualPageRangeStart, false, true, false );
		}
	}

	// Upload pages
	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_UploadTask);
		ParallelFor(UploadTasks.Num(), [&UploadTasks](int32 i)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CopyPageTask);
			const FUploadTask& Task = UploadTasks[i];
			FMemory::Memcpy(Task.Dst, Task.Src, Task.SrcSize);
		#if !DEBUG_TRANSCODE_PAGES_REPEATEDLY
		#if WITH_EDITOR
			Task.PendingPage->SharedBuffer.Reset();
		#else
			check(Task.PendingPage->Request.IsCompleted());
			Task.PendingPage->Request.Reset();
		#endif
		#endif
		});
	}

#if WITH_EDITOR
	// Unlock BulkData
	for (auto it : ResourceToBulkPointer)
	{
		FResources* Resources = it.Key;
		FByteBulkData& BulkData = Resources->StreamablePages;
		BulkData.Unlock();
	}
#endif
}

FRDGBuffer* FStreamingManager::ResizePoolAllocationIfNeeded(FRDGBuilder& GraphBuilder)
{
	const uint32 OldMaxStreamingPages = MaxStreamingPages;

	ClusterPageData.Allocator.Consolidate();
	const uint32 NumRootPages = (uint32)ClusterPageData.Allocator.GetMaxSize();
	const bool bReservedResource = EnumHasAnyFlags(ClusterPageData.DataBuffer->Desc.Usage, EBufferUsageFlags::ReservedResource);

	if (GNaniteStreamingPoolResize != 0)
	{
		UpdatePageConfiguration();
	}

	const bool bAllowGrow = (GNaniteStreamingDynamicallyGrowAllocations != 0);
	const bool bIgnoreInitialRootPages = (GNaniteStreamingReservedResourceIgnoreInitialRootAllocation != 0) && bReservedResource;
	
	uint32 NumAllocatedRootPages;
	if (bReservedResource)
	{
		// Allocate pages in 16MB chunks to reduce the number of page table updates
		const uint32 AllocationGranularityInPages = (16 << 20) / NANITE_ROOT_PAGE_GPU_SIZE;

		NumAllocatedRootPages = bIgnoreInitialRootPages ? 0u : NumInitialRootPages;
		if (NumRootPages > NumAllocatedRootPages)
		{
			NumAllocatedRootPages = FMath::DivideAndRoundUp(NumRootPages, AllocationGranularityInPages) * AllocationGranularityInPages;
			NumAllocatedRootPages = FMath::Min(NumAllocatedRootPages, bAllowGrow ? MaxRootPages : NumInitialRootPages);
		}
	}
	else
	{
		NumAllocatedRootPages = NumInitialRootPages;
		if (NumRootPages > NumInitialRootPages && bAllowGrow)
		{
			NumAllocatedRootPages = FMath::Clamp(RoundUpToSignificantBits(NumRootPages, 2), NumInitialRootPages, MaxRootPages);
		}		
	}

#if DEBUG_ALLOCATION_STRESS_TEST
	NumAllocatedRootPages = NumRootPages;
#endif

	check(NumAllocatedRootPages >= NumRootPages);	// Root pages just don't fit!
	StatNumAllocatedRootPages = NumAllocatedRootPages;

	SET_DWORD_STAT(STAT_NaniteStreaming08_AllocatedRootPages, NumAllocatedRootPages);
	SET_DWORD_STAT(STAT_NaniteStreaming09_RootPageLimit, MaxRootPages);
	SET_FLOAT_STAT(STAT_NaniteStreaming13_AllocatedRootPagesSizeMB, NumAllocatedRootPages * (NANITE_ROOT_PAGE_GPU_SIZE / 1048576.0f));
	
	const uint32 NumAllocatedPages = MaxStreamingPages + NumAllocatedRootPages;
	const uint64 AllocatedPagesSize = (uint64(NumAllocatedRootPages) << NANITE_ROOT_PAGE_GPU_SIZE_BITS) + (uint64(MaxStreamingPages) << NANITE_STREAMING_PAGE_GPU_SIZE_BITS);
	check(NumAllocatedPages <= NANITE_MAX_GPU_PAGES);
	check(AllocatedPagesSize <= (uint64(GetMaxPagePoolSizeInMB()) << 20));

	SET_DWORD_STAT(STAT_NaniteStreaming0A_StreamingPoolPages, MaxStreamingPages);
	SET_FLOAT_STAT(STAT_NaniteStreaming14_StreamingPoolSizeMB, MaxStreamingPages * (NANITE_STREAMING_PAGE_GPU_SIZE / 1048576.0f));
	SET_FLOAT_STAT(STAT_NaniteStreaming12_TotalPoolSizeMB, AllocatedPagesSize / 1048576.0f);
	SET_FLOAT_STAT(STAT_NaniteStreaming15_TotalPoolSizeLimitMB, (float)GetMaxPagePoolSizeInMB());

#if CSV_PROFILER_STATS
	if (ClusterPageData.DataBuffer && AllocatedPagesSize > ClusterPageData.DataBuffer->GetAlignedSize())
	{
		if (!bReservedResource)
		{
			CSV_EVENT(NaniteStreaming, TEXT("GrowPoolAllocation"));
		}
	}
#endif

	FRDGBuffer* ClusterPageDataBuffer = nullptr;

	const bool bResetStreamingState = bClusterPageDataAllocated && ((MaxStreamingPages != OldMaxStreamingPages) || bPendingPoolReset);
	if (bResetStreamingState)
	{
		ClusterPageDataBuffer = GraphBuilder.RegisterExternalBuffer(ClusterPageData.DataBuffer);

		{
			// Uninstall all streaming pages
			check(ClusterScatterUpdates->Num() == 0u);
			check(HierarchyScatterUpdates->Num() == 0u);

			UninstallAllResidentPages(OldMaxStreamingPages);

			const uint32 NumClusterUpdates = ClusterScatterUpdates->Num();

			// Reinstall root pages
			for (uint32 i = 0; i < (uint32)RootPageInfos.Num(); i++)
			{
				if (RootPageInfos[i].RuntimeResourceID != INDEX_NONE && RootPageInfos[i].FixupChunk)	// FixupChunk when the resource has been added, but ProcessNewResources hasn't run yet
				{
					ApplyFixups(*RootPageInfos[i].FixupChunk, *RootPageInfos[i].Resources, nullptr, MaxStreamingPages, MAX_uint32, RootPageInfos[i].VirtualPageRangeStart, false, false, true);
				}
			}

			check(ClusterScatterUpdates->Num() == NumClusterUpdates);	// Root page fixup shouldn't write to any page

			const bool bVerify = VerificationLevel() >= 1;
			ClusterScatterUpdates->ResolveOverwrites(bVerify);					// TODO: Probably not necessary yet, but might be in the future.
			HierarchyScatterUpdates->ResolveOverwrites(bVerify);

			ClusterScatterUpdates->Flush(GraphBuilder, GraphBuilder.CreateUAV(ClusterPageDataBuffer));
			HierarchyScatterUpdates->Flush(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(Hierarchy.DataBuffer)));
		}
		
		const uint32 RootPagesDataSize = NumRootPages * NANITE_ROOT_PAGE_GPU_SIZE;
		if (bReservedResource)
		{
			// Reserved resource path: Move root pages without using temporary memory and commit/decommit physical pages as needed.
			if (MaxStreamingPages < OldMaxStreamingPages)
			{
				// Smaller allocation: Move root pages down then resize
				ClusterPageDataBuffer = GraphBuilder.RegisterExternalBuffer(ClusterPageData.DataBuffer);
				AddPass_Memmove(GraphBuilder, GraphBuilder.CreateUAV(ClusterPageDataBuffer), MaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, OldMaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, RootPagesDataSize);				
				ClusterPageDataBuffer = ResizeByteAddressBufferIfNeededWithCurrentLLMTag(GraphBuilder, ClusterPageData.DataBuffer, AllocatedPagesSize, TEXT("Nanite.StreamingManager.ClusterPageData"));
			}
			else if (MaxStreamingPages > OldMaxStreamingPages)
			{
				// Larger allocation: Resize then move allocation
				ClusterPageDataBuffer = ResizeByteAddressBufferIfNeededWithCurrentLLMTag(GraphBuilder, ClusterPageData.DataBuffer, AllocatedPagesSize, TEXT("Nanite.StreamingManager.ClusterPageData"));
				AddPass_Memmove(GraphBuilder, GraphBuilder.CreateUAV(ClusterPageDataBuffer), MaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, OldMaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, RootPagesDataSize);
			}
		}
		else
		{
			// Non-reserved resource path: Make new allocation and copy root pages over. Temporary peak in memory usage when both allocations need to be live at the same time.

			// TODO: We could lower the theoretical peak memory usage here by copying via a third temporary allocation that is only the size of the root pages.
			//       Investigate if that would even save anything. If RDG overlaps the lifetime of the two buffer ClusterPageData allocations,
			//		 it would actually be worse to introduce a 3rd allocation.
			//		 It might not be worthwhile if reserved resources will be supported on all relevant platforms soon.

			FRDGBuffer* OldClusterPageDataBuffer = GraphBuilder.RegisterExternalBuffer(ClusterPageData.DataBuffer);
			ClusterPageDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(AllocatedPagesSize), TEXT("Nanite.StreamingManager.ClusterPageData"));
			AddCopyBufferPass(GraphBuilder, ClusterPageDataBuffer, MaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, OldClusterPageDataBuffer, OldMaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE, RootPagesDataSize);
			ClusterPageData.DataBuffer = GraphBuilder.ConvertToExternalBuffer(ClusterPageDataBuffer);
		}
		
		// Clear cluster page data just to be sure we aren't accidentally depending on stale data
		FMemsetResourceParams MemsetParams = {};
		MemsetParams.Count = MaxStreamingPages * NANITE_STREAMING_PAGE_GPU_SIZE / 4;
		MemsetParams.Value = 0;
		MemsetParams.DstOffset = 0;
		MemsetResource(GraphBuilder, GraphBuilder.CreateUAV(ClusterPageDataBuffer), MemsetParams);
	
		ResetStreamingStateCPU();		
		bPendingPoolReset = false;
	}
	else
	{
		ClusterPageDataBuffer = ResizeByteAddressBufferIfNeededWithCurrentLLMTag(GraphBuilder, ClusterPageData.DataBuffer, AllocatedPagesSize, TEXT("Nanite.StreamingManager.ClusterPageData"));		
		bClusterPageDataAllocated = true;
	}

	RootPageInfos.SetNum(NumAllocatedRootPages);
	
	check(ClusterPageDataBuffer);
	return ClusterPageDataBuffer;
}

void FStreamingManager::ProcessNewResources(FRDGBuilder& GraphBuilder, FRDGBuffer* ClusterPageDataBuffer)
{
	LLM_SCOPE_BYTAG(Nanite);

	if (PendingAdds.Num() == 0)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::ProcessNewResources);

	// Upload hierarchy for pending resources
	Hierarchy.Allocator.Consolidate();
	const uint32 NumAllocatedHierarchyDwords = FMath::RoundUpToPowerOfTwo(Hierarchy.Allocator.GetMaxSize());
	SET_FLOAT_STAT(STAT_NaniteStreaming11_HiearchySizeMB, NumAllocatedHierarchyDwords * sizeof(uint32) / 1048576.0f);
	FRDGBuffer* HierarchyDataBuffer = ResizeByteAddressBufferIfNeededWithCurrentLLMTag(GraphBuilder, Hierarchy.DataBuffer, NumAllocatedHierarchyDwords * sizeof(uint32), TEXT("Nanite.StreamingManager.Hierarchy"));
	Hierarchy.UploadBuffer.Init(GraphBuilder, Hierarchy.TotalUpload, sizeof(uint32), false, TEXT("Nanite.StreamingManager.HierarchyUpload"));

	FRDGBuffer* ImposterDataBuffer = nullptr;
	const bool bUploadImposters = GNaniteStreamingImposters && ImposterData.TotalUpload > 0;
	if (bUploadImposters)
	{
		check(NANITE_IMPOSTERS_SUPPORTED != 0);
		uint32 WidthInTiles = 12;
		uint32 TileSize = 12;
		uint32 AtlasBytes = FMath::Square( WidthInTiles * TileSize ) * sizeof( uint16 );
		ImposterData.Allocator.Consolidate();
		const uint32 NumAllocatedImposters = FMath::Max( RoundUpToSignificantBits(ImposterData.Allocator.GetMaxSize(), 2), (uint32)GNaniteStreamingNumInitialImposters );
		ImposterDataBuffer = ResizeByteAddressBufferIfNeededWithCurrentLLMTag(GraphBuilder, ImposterData.DataBuffer, NumAllocatedImposters * AtlasBytes, TEXT("Nanite.StreamingManager.ImposterData"));
		ImposterData.UploadBuffer.Init(GraphBuilder, ImposterData.TotalUpload, AtlasBytes, false, TEXT("Nanite.StreamingManager.ImposterDataUpload"));

		SET_FLOAT_STAT(STAT_NaniteStreaming10_ImpostersSizeMB, NumAllocatedImposters * AtlasBytes / 1048576.0f);
	}

	// Calculate total required size
	uint32 TotalPageSize = 0;
	uint32 TotalRootPages = 0;
	for (FResources* Resources : PendingAdds)
	{
		for (uint32 i = 0; i < Resources->NumRootPages; i++)
		{
			TotalPageSize += Resources->PageStreamingStates[i].PageSize;
		}

		TotalRootPages += Resources->NumRootPages;
	}

	FStreamingPageUploader RootPageUploader;
	RootPageUploader.Init(GraphBuilder, TotalRootPages, TotalPageSize, MaxStreamingPages);

	GPUPageDependencies.Reset();

	for (FResources* Resources : PendingAdds)
	{
		Resources->NumResidentClusters = 0;

		for (uint32 LocalPageIndex = 0; LocalPageIndex < Resources->NumRootPages; LocalPageIndex++)
		{
			const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[LocalPageIndex];

			const uint32 RootPageIndex = Resources->RootPageIndex + LocalPageIndex;
			const uint32 GPUPageIndex = MaxStreamingPages + RootPageIndex;

			const uint8* Ptr = Resources->RootData.GetData() + PageStreamingState.BulkOffset;
			const uint32 FixupChunkSize = ((const FFixupChunk*)Ptr)->GetSize();
			FFixupChunk* FixupChunk = (FFixupChunk*)FMemory::Malloc(FixupChunkSize, sizeof(uint16));
			FMemory::Memcpy(FixupChunk, Ptr, FixupChunkSize);

			const uint32 NumClusters = FixupChunk->Header.NumClusters;

			const FPageKey GPUPageKey = { Resources->RuntimeResourceID, GPUPageIndex };

			const uint32 PageDiskSize = PageStreamingState.PageSize;
			check(PageDiskSize == PageStreamingState.BulkSize - FixupChunkSize);
			const uint32 PageOffset = GPUPageIndexToGPUOffset(MaxStreamingPages, GPUPageIndex);

			uint8* Dst = RootPageUploader.Add_GetRef(PageDiskSize, NumClusters, PageOffset, GPUPageKey, GPUPageDependencies);
			FMemory::Memcpy(Dst, Ptr + FixupChunkSize, PageDiskSize);

			// Root node should only have fixups that depend on other non-root pages and cannot be satisfied yet.

			FRootPageInfo& RootPageInfo = RootPageInfos[RootPageIndex];
			RootPageInfo.RuntimeResourceID = Resources->RuntimeResourceID;
			RootPageInfo.FixupChunk = FixupChunk;
			RootPageInfo.MaxHierarchyDepth = PageStreamingState.MaxHierarchyDepth;
			HierarchyDepthManager->Add(PageStreamingState.MaxHierarchyDepth);
			
			// Fixup hierarchy
			for (uint32 i = 0; i < FixupChunk->Header.NumGroupFixups; i++)
			{
				FFixupChunk::FGroupFixup& GroupFixup = FixupChunk->GetGroupFixup(i);
				// TODO: Unify this with Applyfixup	?
				
				// Only install part if it has no streaming page dependencies
				if (!GroupFixup.PageDependencies.HasStreamingPages())
				{
					for (uint32 j = 0; j < GroupFixup.NumPartFixups; j++)
					{
						const FFixupChunk::FPartFixup& PartFixup = FixupChunk->GetPartFixup(GroupFixup.FirstPartFixup + j);

						for (uint32 k = 0; k < PartFixup.NumHierarchyLocations; k++)
						{
							const FFixupChunk::FHierarchyLocation& HierarchyLocation = FixupChunk->GetHierarchyLocation(PartFixup.FirstHierarchyLocation + k);

							const uint32 HierarchyNodeIndex = HierarchyLocation.GetNodeIndex();
							check(HierarchyNodeIndex < (uint32)Resources->HierarchyNodes.Num());
							const uint32 ChildIndex = HierarchyLocation.GetChildIndex();

							const uint32 TargetGPUPageIndex = MaxStreamingPages + Resources->RootPageIndex + PartFixup.PageIndex;
							const uint32 ChildStartReference = (TargetGPUPageIndex << NANITE_MAX_CLUSTERS_PER_PAGE_BITS) | PartFixup.StartClusterIndex;

							Resources->HierarchyNodes[HierarchyNodeIndex].Misc1[ChildIndex].ChildStartReference = ChildStartReference;
						}
					}
				}
				GroupFixup.Flags |= NANITE_FIXUP_FLAG_INSTALLED;
			}

			Resources->NumResidentClusters += NumClusters; // clusters in root pages are always streamed in
		}

		ModifiedResources.Add(Resources->RuntimeResourceID, Resources->NumResidentClusters);

		const uint32 HierarchyNodeSizeDwords = Resources->HierarchyNodes.Num() * NANITE_HIERARCHY_NODE_SLICE_SIZE_DWORDS;
		const uint32 AssemblyTransformSizeDwords = Resources->AssemblyTransforms.Num() * NANITE_ASSEMBLY_TRANSFORM_SIZE_DWORDS;
		const uint32 AssemblyBoneAttachmentSizeDwords = Resources->AssemblyBoneAttachmentData.Num();
		Hierarchy.UploadBuffer.Add(Resources->HierarchyOffset, Resources->HierarchyNodes.GetData(), HierarchyNodeSizeDwords);
		if (AssemblyTransformSizeDwords > 0)
		{
			Hierarchy.UploadBuffer.Add(Resources->HierarchyOffset + HierarchyNodeSizeDwords, Resources->AssemblyTransforms.GetData(), AssemblyTransformSizeDwords);
		}
		if (AssemblyBoneAttachmentSizeDwords > 0)
		{
			Hierarchy.UploadBuffer.Add(Resources->HierarchyOffset + HierarchyNodeSizeDwords + AssemblyTransformSizeDwords, Resources->AssemblyBoneAttachmentData.GetData(), AssemblyBoneAttachmentSizeDwords);
		}
		if (bUploadImposters && Resources->ImposterAtlas.Num() > 0)
		{
			ImposterData.UploadBuffer.Add(Resources->ImposterIndex, Resources->ImposterAtlas.GetData());
		}

		// We can't free the CPU data in editor builds because the resource might be kept around and used for cooking later.
	#if !WITH_EDITOR
		Resources->RootData.Empty();
		Resources->HierarchyNodes.Empty();
		Resources->ImposterAtlas.Empty();
	#endif
	}

	{
		Hierarchy.TotalUpload = 0;
		Hierarchy.UploadBuffer.ResourceUploadTo(GraphBuilder, HierarchyDataBuffer);

		RootPageUploader.ResourceUploadTo(GraphBuilder, ClusterPageDataBuffer);

		if (bUploadImposters)
		{
			ImposterData.TotalUpload = 0;
			ImposterData.UploadBuffer.ResourceUploadTo(GraphBuilder, ImposterDataBuffer);
		}
	}

	PendingAdds.Reset();
}

struct FStreamingUpdateParameters
{
	FStreamingManager* StreamingManager = nullptr;
};

class FStreamingUpdateTask
{
public:
	explicit FStreamingUpdateTask(const FStreamingUpdateParameters& InParams) : Parameters(InParams) {}

	FStreamingUpdateParameters Parameters;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Parameters.StreamingManager->AsyncUpdate();
	}

	static ESubsequentsMode::Type	GetSubsequentsMode()	{ return ESubsequentsMode::TrackSubsequents; }
	ENamedThreads::Type				GetDesiredThread()		{ return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId				GetStatId() const		{ return TStatId(); }
};

uint32 FStreamingManager::DetermineReadyOrSkippedPages(uint32& TotalPageSize)
{
	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::DetermineReadyPages);

	const uint32 StartPendingPageIndex = (NextPendingPageIndex + MaxPendingPages - NumPendingPages) % MaxPendingPages;
	uint32 NumReadyOrSkippedPages = 0;
	
	uint64 UpdateTick = FPlatformTime::Cycles64();
	uint64 DeltaTick = PrevUpdateTick ? UpdateTick - PrevUpdateTick : 0;
	PrevUpdateTick = UpdateTick;

	TotalPageSize = 0;
	// Check how many pages are ready
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CheckReadyPages);

		for( uint32 i = 0; i < NumPendingPages && NumReadyOrSkippedPages < MaxPageInstallsPerUpdate; i++ )
		{
			uint32 PendingPageIndex = ( StartPendingPageIndex + i ) % MaxPendingPages;
			FPendingPage& PendingPage = PendingPages[ PendingPageIndex ];
			bool bFreePageFromStagingAllocator = false;
#if WITH_EDITOR
			if (PendingPage.State == FPendingPage::EState::DDC_Ready)
			{
				if (PendingPage.RetryCount > 0)
				{
					FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
					if (Resources)
					{
						UE_LOG(LogNaniteStreaming, Log, TEXT("Nanite DDC retry succeeded for '%s' (Page %d) after %d attempts."), *Resources->ResourceName, PendingPage.InstallKey.PageIndex, PendingPage.RetryCount);
					}
				}
			}
			else if (PendingPage.State == FPendingPage::EState::DDC_Pending)
			{
				break;
			}
			else if (PendingPage.State == FPendingPage::EState::DDC_Failed)
			{
				FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
				if (Resources)
				{
					// Resource is still there. Retry the request.
					PendingPage.State = FPendingPage::EState::DDC_Pending;
					PendingPage.RetryCount++;
					
					if(PendingPage.RetryCount == 1)	// Only warn on first retry to prevent spam
					{
						UE_LOG(LogNaniteStreaming, Log, TEXT("Nanite DDC request failed for '%s' (Page %d)."), *Resources->ResourceName, PendingPage.InstallKey.PageIndex);
					}
					
					if (GNaniteStreamingNumRetries < 0 || PendingPage.RetryCount <= (uint32)GNaniteStreamingNumRetries)
					{
						UE_LOG(LogNaniteStreaming, Log, TEXT("Retrying Nanite DDC request for '%s' (Page %d)."), *Resources->ResourceName, PendingPage.InstallKey.PageIndex);

						const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[PendingPage.InstallKey.PageIndex];
						FCacheGetChunkRequest Request = BuildDDCRequest(*Resources, PageStreamingState, PendingPageIndex);
						RequestDDCData(MakeArrayView(&Request, 1));
						break;
					}
					else
					{
						UE_LOG(LogNaniteStreaming, Warning, TEXT("Too many Nanite DDC retries for '%s' (Page %d). Giving up and marking resource invalid."), *Resources->ResourceName, PendingPage.InstallKey.PageIndex);
						RootPageInfos[Resources->RootPageIndex].bInvalidResource = true;
						// Skip page. Just keep State as DDC_Failed as bInvalidResource overrides State.
					}
				}
				else
				{
					// Resource is no longer there. Just mark as ready so it will be skipped in InstallReadyPages
					PendingPage.State = FPendingPage::EState::DDC_Ready;
					break;
				}
			}
			else if (PendingPage.State == FPendingPage::EState::Memory)
			{
				// Memory is always ready
			}
			else
#endif
			{
#if WITH_EDITOR
				check(PendingPage.State == FPendingPage::EState::Disk);
#endif
				if (PendingPage.Request.IsCompleted())
				{
					if (!PendingPage.Request.IsOk())
					{
						// Retry if IO request failed for some reason
						FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
						if (Resources)	// If the resource is gone, no need to do anything as the page will be ignored by InstallReadyPages
						{
							const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[PendingPage.InstallKey.PageIndex];
							PendingPage.RetryCount++;
							if (PendingPage.RetryCount == 1)
							{
								UE_LOG(LogNaniteStreaming, Warning, TEXT("IO Request failed. RuntimeResourceID: %8X, Offset: %d, Size: %d."), PendingPage.InstallKey.RuntimeResourceID, PageStreamingState.BulkOffset, PageStreamingState.BulkSize);
							}

							if (GNaniteStreamingNumRetries < 0 || PendingPage.RetryCount <= (uint32)GNaniteStreamingNumRetries)
							{
								UE_LOG(LogNaniteStreaming, Log, TEXT("Retrying IO request RuntimeResourceID: %8X, Offset: %d, Size: %d."), PendingPage.InstallKey.RuntimeResourceID, PageStreamingState.BulkOffset, PageStreamingState.BulkSize);

								TRACE_IOSTORE_METADATA_SCOPE_TAG("NaniteReadyPages");
								FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(1);
								Batch.Read(Resources->StreamablePages, PageStreamingState.BulkOffset, PageStreamingState.BulkSize, AIOP_Low, PendingPage.RequestBuffer, PendingPage.Request);
								(void)Batch.Issue();
								break;
							}
							else
							{
								UE_LOG(LogNaniteStreaming, Warning, TEXT("Too many Nanite IO request retries for RuntimeResourceID: %8X, Offset: %d, Size: %d. Giving up and marking resource invalid."), PendingPage.InstallKey.RuntimeResourceID, PageStreamingState.BulkOffset, PageStreamingState.BulkSize);
								RootPageInfos[Resources->RootPageIndex].bInvalidResource = true;
							}
						}
					}
					else
					{
						if (PendingPage.RetryCount > 0)
						{
							FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
							if (Resources)
							{
								const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[PendingPage.InstallKey.PageIndex];
								UE_LOG(LogNaniteStreaming, Log, TEXT("Nanite IO request retry succeeded for RuntimeResourceID: %8X, Offset: %d, Size: %d after %d attempts."), PendingPage.InstallKey.RuntimeResourceID, PageStreamingState.BulkOffset, PageStreamingState.BulkSize, PendingPage.RetryCount);
							}
						}
					}

				#if !DEBUG_TRANSCODE_PAGES_REPEATEDLY
					bFreePageFromStagingAllocator = true;
				#endif
				}
				else
				{
					break;
				}
			}

			if(GNaniteStreamingBandwidthLimit >= 0.0f)
			{
				uint32 SimulatedBytesRemaining = FPlatformTime::ToSeconds64(DeltaTick) * GNaniteStreamingBandwidthLimit * 1048576.0;
				uint32 SimulatedBytesRead = FMath::Min(PendingPage.BytesLeftToStream, SimulatedBytesRemaining);
				PendingPage.BytesLeftToStream -= SimulatedBytesRead;
				SimulatedBytesRemaining -= SimulatedBytesRead;
				if(PendingPage.BytesLeftToStream > 0)
					break;
			}


			if(bFreePageFromStagingAllocator)
			{
				PendingPageStagingAllocator->Free(PendingPage.RingBufferAllocationSize);
			}

			FResources* Resources = GetResources(PendingPage.InstallKey.RuntimeResourceID);
			if (Resources && !RootPageInfos[Resources->RootPageIndex].bInvalidResource)
			{
				const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[PendingPage.InstallKey.PageIndex];
				TotalPageSize += PageStreamingState.PageSize;
			}

			NumReadyOrSkippedPages++;
		}
	}
	
	return NumReadyOrSkippedPages;
}

void FStreamingManager::AddPendingExplicitRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AddPendingExplicitRequests);

	const int32 NumPendingExplicitRequests = PendingExplicitRequests.Num();
	if (NumPendingExplicitRequests == 0)
	{
		return;
	}

	uint32 NumPageRequests = 0;
	int32 Index = 0;
	while (Index < NumPendingExplicitRequests)
	{
		const uint32 ResourcePersistentHash = PendingExplicitRequests[Index++];
			
		// Resolve resource
		TArray<FResources*, TInlineAllocator<16>> MultiMapResult;
		PersistentHashResourceMap.MultiFind(ResourcePersistentHash, MultiMapResult);

		// Keep processing requests from this resource as long as they have the repeat bit set
		bool bRepeat = true;
		while (bRepeat && Index < NumPendingExplicitRequests)
		{
			const uint32 Packed = PendingExplicitRequests[Index++];
			bRepeat = (Packed & 1u) != 0u;
				
			// Add requests to table
			// In the rare event of a collision all resources with the same hash will be requested
			for (const FResources* Resources : MultiMapResult)
			{
				const uint32 PageIndex = (Packed >> 1) & NANITE_MAX_RESOURCE_PAGES_MASK;
				const uint32 Priority = FMath::Min(Packed | ((1 << (NANITE_MAX_RESOURCE_PAGES_BITS + 1)) - 1), NANITE_MAX_PRIORITY_BEFORE_PARENTS);	// Round quantized priority up
				if (PageIndex >= Resources->NumRootPages && PageIndex < (uint32)Resources->PageStreamingStates.Num())
				{
					AddRequest(Resources->RuntimeResourceID, PageIndex, Priority);
					NumPageRequests++;
				}
			}
		}
	}
	PendingExplicitRequests.Reset();

	INC_DWORD_STAT_BY(STAT_NaniteStreaming20_PageRequests, NumPageRequests);
	SET_DWORD_STAT(STAT_NaniteStreaming22_PageRequestsExplicit, NumPageRequests);
}

void FStreamingManager::AddPendingResourcePrefetchRequests()
{
	if (PendingResourcePrefetches.Num() == 0)
	{
		return;
	}
		
	uint32 NumPageRequests = 0;
	for (FResourcePrefetch& Prefetch : PendingResourcePrefetches)
	{
		FResources* Resources = GetResources(Prefetch.RuntimeResourceID);
		if (Resources)
		{
			// Request first MAX_RESOURCE_PREFETCH_PAGES streaming pages of resource
			const uint32 NumRootPages = Resources->NumRootPages;
			const uint32 NumPages = Resources->PageStreamingStates.Num();
			const uint32 EndPage = FMath::Min(NumPages, NumRootPages + MAX_RESOURCE_PREFETCH_PAGES);
			
			NumPageRequests += EndPage - NumRootPages;
			
			for (uint32 PageIndex = NumRootPages; PageIndex < EndPage; PageIndex++)
			{
				const uint32 Priority = NANITE_MAX_PRIORITY_BEFORE_PARENTS - Prefetch.NumFramesUntilRender;	// Prefetching has highest priority. Prioritize requests closer to the deadline higher.
																											// TODO: Calculate appropriate priority based on bounds

				AddRequest(Prefetch.RuntimeResourceID, PageIndex, Priority);
			}
		}
		Prefetch.NumFramesUntilRender--;	// Keep the request alive until projected first render
	}

	INC_DWORD_STAT_BY(STAT_NaniteStreaming20_PageRequests, NumPageRequests);
	SET_DWORD_STAT(STAT_NaniteStreaming23_PageRequestsPrefetch, NumPageRequests);

	// Remove requests that are past the rendering deadline
	PendingResourcePrefetches.RemoveAll([](const FResourcePrefetch& Prefetch) { return Prefetch.NumFramesUntilRender == 0; });
}

void FStreamingManager::BeginAsyncUpdate(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	const uint64 FrameNumber = GFrameCounterRenderThread;

	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::BeginAsyncUpdate);

	RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteStreaming, "Nanite::Streaming");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteStreaming);

	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_BeginAsyncUpdate);
	
	check(!AsyncState.bUpdateActive);
	AsyncState = FAsyncState {};
	AsyncState.bUpdateActive = true;

	VirtualPageAllocator.Consolidate();
	RegisteredVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());
	ResidentVirtualPages.SetNum(VirtualPageAllocator.GetMaxSize());

	FRDGBuffer* ClusterPageDataBuffer = ResizePoolAllocationIfNeeded(GraphBuilder);
	ProcessNewResources(GraphBuilder, ClusterPageDataBuffer);

	CSV_CUSTOM_STAT(NaniteStreaming, RootAllocationMB, StatNumAllocatedRootPages * (NANITE_ROOT_PAGE_GPU_SIZE / 1048576.0f), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NaniteStreaming, RootDataSizeMB, ClusterPageData.Allocator.GetMaxSize() * (NANITE_ROOT_PAGE_GPU_SIZE / 1048576.0f), ECsvCustomStatOp::Set);

	if (FrameNumber != PrevUpdateFrameNumber)
	{
		{
			RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteReadback, "Nanite::Readback");
			RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteReadback);

			ReadbackManager->QueueReadback(GraphBuilder);
		}

		uint32 TotalPageSize;
		AsyncState.NumReadyOrSkippedPages = DetermineReadyOrSkippedPages(TotalPageSize);
		if (AsyncState.NumReadyOrSkippedPages > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(AllocBuffers);
			// Prepare buffers for upload
			PageUploader->Init(GraphBuilder, AsyncState.NumReadyOrSkippedPages, TotalPageSize, MaxStreamingPages);

			check(ClusterScatterUpdates->Num() == 0u);
			check(HierarchyScatterUpdates->Num() == 0u);
		}

		uint32 NumGPUStreamingRequestsUnclamped;
		AsyncState.GPUStreamingRequestsPtr = ReadbackManager->LockLatest(AsyncState.NumGPUStreamingRequests, NumGPUStreamingRequestsUnclamped);
		const uint32 RequestsBufferSize = ReadbackManager->PrepareRequestsBuffer(GraphBuilder);

		PrevUpdateFrameNumber = FrameNumber;

		SET_DWORD_STAT(STAT_NaniteStreaming41_ReadbackSize, NumGPUStreamingRequestsUnclamped);
		SET_DWORD_STAT(STAT_NaniteStreaming42_ReadbackBufferSize, RequestsBufferSize);
	}

	if(AsyncState.GPUStreamingRequestsPtr || AsyncState.NumReadyOrSkippedPages > 0)
	{
		// Start async processing
		FStreamingUpdateParameters Parameters;
		Parameters.StreamingManager = this;

		check(AsyncTaskEvents.IsEmpty());
		if (GNaniteStreamingAsync)
		{
			AsyncTaskEvents.Add(TGraphTask<FStreamingUpdateTask>::CreateTask().ConstructAndDispatchWhenReady(Parameters));
		}
		else
		{
			AsyncUpdate();
		}
	}
}

#if NANITE_SANITY_CHECK_STREAMING_REQUESTS
void FStreamingManager::SanityCheckStreamingRequests(const FGPUStreamingRequest* StreamingRequestsPtr, const uint32 NumStreamingRequests)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SanityCheckRequests);
	uint32 PrevFrameNibble = ~0u;
	for (uint32 Index = 0; Index < NumStreamingRequests; Index++)
	{
		const FGPUStreamingRequest& GPURequest = StreamingRequestsPtr[Index];

		// Validate request magics
		if ((GPURequest.RuntimeResourceID_Magic & 0x30) != 0x10 ||
			(GPURequest.Priority_Magic & 0x30) != 0x20)
		{
			UE_LOG(LogNaniteStreaming, Fatal, TEXT("Validation of Nanite streaming request failed! The magic doesn't match. This likely indicates an issue with the GPU readback."));
		}

		// Validate that requests are from the same frame
		const uint32 FrameNibble0 = GPURequest.RuntimeResourceID_Magic & 0xF;
		const uint32 FrameNibble1 = GPURequest.Priority_Magic & 0xF;
		if (FrameNibble0 != FrameNibble1 || (PrevFrameNibble != ~0u && FrameNibble0 != PrevFrameNibble))
		{
			UE_LOG(LogNaniteStreaming, Fatal, TEXT("Validation of Nanite streaming request failed! Single readback has data from multiple frames. Is there a race condition on the readback, a missing streaming update or is GPUScene being updated mid-frame?"));
		}
		PrevFrameNibble = FrameNibble0;

		FResources* Resources = GetResources(GPURequest.RuntimeResourceID_Magic >> NANITE_STREAMING_REQUEST_MAGIC_BITS);
		if (Resources)
		{
			if (GPURequest.ResourcePageRangeKey.IsEmpty())
			{
				UE_LOG(LogNaniteStreaming, Fatal, TEXT("Validation of Nanite streaming request failed! Page lookup key is empty."));
			}

			if (!GPURequest.ResourcePageRangeKey.HasStreamingPages())
			{
				UE_LOG(LogNaniteStreaming, Fatal, TEXT("Validation of Nanite streaming request failed! Request has no streaming pages."));
			}
			
			if (!Resources->IsValidPageRangeKey(GPURequest.ResourcePageRangeKey))
			{
				UE_LOG(LogNaniteStreaming, Fatal, TEXT("Validation of Nanite streaming request failed! Page lookup key is not valid."));
			}
		}
	}
}
#endif

bool FStreamingManager::AddRequest(uint32 RuntimeResourceID, uint32 PageIndex, uint32 VirtualPageIndex, uint32 Priority)
{
	check(Priority != 0u);

	FRegisteredVirtualPage& VirtualPage = RegisteredVirtualPages[VirtualPageIndex];
	if (VirtualPage.RegisteredPageIndex != INDEX_NONE)
	{
		if (VirtualPage.Priority == 0u)
		{
			RequestedRegisteredPages.Add(VirtualPageIndex);
		}
	}
	else
	{
		if (VirtualPage.Priority == 0u)
		{
			RequestedNewPages.Add(FNewPageRequest{ FPageKey{ RuntimeResourceID, PageIndex }, VirtualPageIndex});
		}
	}

	const bool bUpdatedPriority = Priority > VirtualPage.Priority;
	VirtualPage.Priority = bUpdatedPriority ? Priority : VirtualPage.Priority;
	return bUpdatedPriority;
}

bool FStreamingManager::AddRequest(uint32 RuntimeResourceID, uint32 PageIndex, uint32 Priority)
{
	const FRootPageInfo* RootPageInfo = GetRootPage(RuntimeResourceID);
	if (RootPageInfo)
	{
		return AddRequest(RuntimeResourceID, PageIndex, RootPageInfo->VirtualPageRangeStart + PageIndex, Priority);
	}
	return false;
}

void FStreamingManager::AddPendingGPURequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AddPendingGPURequests);
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_ProcessGPURequests);

	// Update priorities
	const uint32 NumStreamingRequests = AsyncState.NumGPUStreamingRequests;
	if (NumStreamingRequests == 0)
	{
		return;
	}

	class FUpdatePagePriority
	{
	public:
		// NOTE: It is crucial for performance that this is inlined, but that doesn't happen consistently if it is a lambda.
		FORCEINLINE void operator()(FStreamingManager& StreamingManager, uint32 RuntimeResourceID, uint32 VirtualPageRangeStart, uint32 PageIndex, uint32 Priority)
		{
			const uint32 VirtualPageIndex = VirtualPageRangeStart + PageIndex;
			FRegisteredVirtualPage& VirtualPage = StreamingManager.RegisteredVirtualPages[VirtualPageIndex];
			if (VirtualPage.RegisteredPageIndex != INDEX_NONE)
			{
				if (VirtualPage.Priority == 0u)
				{
					StreamingManager.RequestedRegisteredPages.Add(VirtualPageIndex);
				}
			}
			else
			{
				if (VirtualPage.Priority == 0u)
				{
					StreamingManager.RequestedNewPages.Add(FNewPageRequest{ FPageKey { RuntimeResourceID, PageIndex }, VirtualPageIndex });
				}
			}

			VirtualPage.Priority = FMath::Max(VirtualPage.Priority, Priority);	// TODO: Preserve old behavior. We should redo priorities to accumulation
		}
	} UpdatePagePriority;

	const FGPUStreamingRequest* StreamingRequestsPtr = AsyncState.GPUStreamingRequestsPtr;
#if NANITE_SANITY_CHECK_STREAMING_REQUESTS
	SanityCheckStreamingRequests(StreamingRequestsPtr, NumStreamingRequests);
#endif
	const FGPUStreamingRequest* StreamingRequestsEndPtr = StreamingRequestsPtr + NumStreamingRequests;

	const bool bVerify = VerificationLevel() >= 2;

	do
	{
		const FGPUStreamingRequest& GPURequest = *StreamingRequestsPtr;
#if NANITE_SANITY_CHECK_STREAMING_REQUESTS
		const uint32 RuntimeResourceID = (GPURequest.RuntimeResourceID_Magic >> NANITE_STREAMING_REQUEST_MAGIC_BITS);
		const uint32 Priority = GPURequest.Priority_Magic & ~NANITE_STREAMING_REQUEST_MAGIC_MASK;
#else
		const uint32 RuntimeResourceID = GPURequest.RuntimeResourceID_Magic;
		const uint32 Priority = GPURequest.Priority_Magic;
#endif
		const FPageRangeKey PageRangeKey = GPURequest.ResourcePageRangeKey;

		const FRootPageInfo* RootPageInfo = GetRootPage(RuntimeResourceID);
		if (RootPageInfo && !RootPageInfo->bInvalidResource)
		{
			if (Priority == 0u || Priority > NANITE_MAX_PRIORITY_BEFORE_PARENTS)
			{
				if (bVerify)
				{
					UE_LOG(LogNaniteStreaming, Warning, TEXT("Invalid priority %u of request for resource (%8X, %s)."), Priority, RuntimeResourceID, GetNaniteResourceName(*RootPageInfo->Resources));
				}
				continue;
			}

			const uint32 VirtualPageRangeStart = RootPageInfo->VirtualPageRangeStart;
			if (!PageRangeKey.IsMultiRange())
			{
				// Fast single range path
				const uint32 StartPage = PageRangeKey.GetStartIndex();
				const uint32 EndPage = StartPage + PageRangeKey.GetNumPagesOrRanges();
				
				const uint32 ClampedStartPage	= FMath::Max(StartPage, RootPageInfo->NumRootPages);

				if (ClampedStartPage < EndPage && EndPage <= RootPageInfo->NumTotalPages)
				{
					for (uint32 PageIndex = ClampedStartPage; PageIndex < EndPage; PageIndex++)
					{
						UpdatePagePriority(*this, RuntimeResourceID, VirtualPageRangeStart, PageIndex, Priority);
					}
				}
				else if (bVerify)
				{
					UE_LOG(LogNaniteStreaming, Warning, TEXT("Invalid page range request (%d-%d) for resource (%8X, %s) which has %d root pages and %d total pages."),
						StartPage, EndPage - 1,
						RuntimeResourceID, GetNaniteResourceName(*RootPageInfo->Resources),
						RootPageInfo->NumRootPages, RootPageInfo->NumTotalPages);
				}
			}
			else
			{
				const FResources* Resources = RootPageInfo->Resources;

				const uint32 StartRange = PageRangeKey.GetStartIndex();
				const uint32 EndRange = StartRange + PageRangeKey.GetNumPagesOrRanges();

				if (EndRange <= (uint32)Resources->PageRangeLookup.Num())
				{
					Resources->ForEachPage(
						PageRangeKey,
						[this, RuntimeResourceID, Priority, VirtualPageRangeStart, &UpdatePagePriority, RootPageInfo, bVerify](uint32 PageIndex)
						{
							if (PageIndex >= RootPageInfo->NumRootPages && PageIndex < RootPageInfo->NumTotalPages)
							{
								UpdatePagePriority(*this, RuntimeResourceID, VirtualPageRangeStart, PageIndex, Priority);
							}
							else if (bVerify)
							{
								UE_LOG(LogNaniteStreaming, Warning, TEXT("Invalid page request (%d) for resource (%8X, %s) which has %d root pages and %d total pages."),
									PageIndex,
									RuntimeResourceID, GetNaniteResourceName(*RootPageInfo->Resources),
									RootPageInfo->NumRootPages, RootPageInfo->NumTotalPages);
							}
						},
						true);
				}
				else if(bVerify)
				{
					UE_LOG(LogNaniteStreaming, Warning, TEXT("Invalid page multi range request (%d-%d) for resource (%8X, %s) which has %d page range lookups."),
						StartRange, EndRange - 1,
						RuntimeResourceID, GetNaniteResourceName(*RootPageInfo->Resources),
						Resources->PageRangeLookup.Num());
				}
			}
		}
	} while (++StreamingRequestsPtr < StreamingRequestsEndPtr);

	INC_DWORD_STAT_BY(STAT_NaniteStreaming20_PageRequests, NumStreamingRequests);
	SET_DWORD_STAT(STAT_NaniteStreaming21_PageRequestsGPU, NumStreamingRequests);
}

void FStreamingManager::AddParentNewRequestsRecursive(const FResources& Resources, uint32 RuntimeResourceID, uint32 PageIndex, uint32 VirtualPageRangeStart, uint32 Priority)
{
	checkSlow(Priority < MAX_uint32);
	const uint32 NextPriority = Priority + 1u;

	const FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];
	for (uint32 i = 0; i < PageStreamingState.DependenciesNum; i++)
	{
		const uint32 DependencyPageIndex = Resources.PageDependencies[PageStreamingState.DependenciesStart + i];
		if (!Resources.IsRootPage(DependencyPageIndex))
		{
			if (AddRequest(RuntimeResourceID, DependencyPageIndex, VirtualPageRangeStart + DependencyPageIndex, NextPriority))
			{
				AddParentNewRequestsRecursive(Resources, RuntimeResourceID, DependencyPageIndex, VirtualPageRangeStart, NextPriority);
			}
		}
	}
}

void FStreamingManager::AddParentRegisteredRequestsRecursive(uint32 RegisteredPageIndex, uint32 Priority)
{
	checkSlow(Priority < MAX_uint32);
	const uint32 NextPriority = Priority + 1u;
	
	const FRegisteredPageDependencies& Dependencies = RegisteredPageDependencies[RegisteredPageIndex];
	for (uint32 DependencyVirtualPageIndex : Dependencies)
	{
		FRegisteredVirtualPage& DependencyVirtualPage = RegisteredVirtualPages[DependencyVirtualPageIndex];

		if (DependencyVirtualPage.Priority == 0u)
		{
			RequestedRegisteredPages.Add(DependencyVirtualPageIndex);
		}
		
		if (NextPriority > DependencyVirtualPage.Priority)
		{
			DependencyVirtualPage.Priority = NextPriority;
			AddParentRegisteredRequestsRecursive(DependencyVirtualPage.RegisteredPageIndex, NextPriority);
		}
	}
}

// Add implicit requests for any parent pages that were not already referenced
void FStreamingManager::AddParentRequests()
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_AddParentRequests);
	
	// Process new pages first as they might add references to already registered pages.
	// An already registered page will never have a dependency on a new page.
	if (RequestedNewPages.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_AddParentNewRequests);
		const uint32 NumInitialRequests = RequestedNewPages.Num();
		for (uint32 i = 0; i < NumInitialRequests; i++)
		{
			FNewPageRequest Request = RequestedNewPages[i];	// Needs to be a copy as the array can move
			checkSlow(RegisteredVirtualPages[Request.VirtualPageIndex].RegisteredPageIndex == INDEX_NONE);

			FRootPageInfo* RootPage = GetRootPage(Request.Key.RuntimeResourceID);
			const uint32 Priority = RegisteredVirtualPages[Request.VirtualPageIndex].Priority;
			AddParentNewRequestsRecursive(*RootPage->Resources, Request.Key.RuntimeResourceID, Request.Key.PageIndex, RootPage->VirtualPageRangeStart, Priority);	//Make it non-recursive
		}
	}

	if (RequestedRegisteredPages.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_AddParentRegisteredRequests);
		const uint32 NumInitialRequests = RequestedRegisteredPages.Num();
		for (uint32 i = 0; i < NumInitialRequests; i++)
		{
			const uint32 VirtualPageIndex = RequestedRegisteredPages[i];
			const FRegisteredVirtualPage& VirtualPage = RegisteredVirtualPages[VirtualPageIndex];

			checkSlow(VirtualPage.Priority <= NANITE_MAX_PRIORITY_BEFORE_PARENTS);
			const uint32 NextPriority = VirtualPage.Priority + 1u;
			const FRegisteredPageDependencies& Dependencies = RegisteredPageDependencies[VirtualPage.RegisteredPageIndex];
			for (uint32 DependencyVirtualPageIndex : Dependencies)
			{
				FRegisteredVirtualPage& DependencyVirtualPage = RegisteredVirtualPages[DependencyVirtualPageIndex];

				if (DependencyVirtualPage.Priority == 0u)
				{
					RequestedRegisteredPages.Add(DependencyVirtualPageIndex);
				}

				if (NextPriority > DependencyVirtualPage.Priority)
				{
					DependencyVirtualPage.Priority = NextPriority;
					AddParentRegisteredRequestsRecursive(DependencyVirtualPage.RegisteredPageIndex, NextPriority);
				}
			}
		}
	}
}

void FStreamingManager::MoveToEndOfLRUList(uint32 RegisteredPageIndex)
{
	uint32& LRUIndex = RegisteredPageIndexToLRU[RegisteredPageIndex];
	check(LRUIndex != INDEX_NONE);
	check((LRUToRegisteredPageIndex[LRUIndex] & LRU_INDEX_MASK) == RegisteredPageIndex);

	LRUToRegisteredPageIndex[LRUIndex] = INDEX_NONE;
	LRUIndex = LRUToRegisteredPageIndex.Num();
	LRUToRegisteredPageIndex.Add(RegisteredPageIndex | LRU_FLAG_REFERENCED_THIS_UPDATE);
}

void FStreamingManager::CompactLRU()
{
	//TODO: Make it so uninstalled pages are moved to the front of the queue immediately
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_CompactLRU);
	uint32 WriteIndex = 0;
	const uint32 LRUBufferLength = LRUToRegisteredPageIndex.Num();
	for (uint32 i = 0; i < LRUBufferLength; i++)
	{
		const uint32 Entry = LRUToRegisteredPageIndex[i];
		if (Entry != INDEX_NONE)
		{
			const uint32 RegisteredPageIndex = Entry & LRU_INDEX_MASK;
			LRUToRegisteredPageIndex[WriteIndex] = RegisteredPageIndex;
			RegisteredPageIndexToLRU[RegisteredPageIndex] = WriteIndex;
			WriteIndex++;
		}
	}
	check(WriteIndex == MaxStreamingPages);
	LRUToRegisteredPageIndex.SetNum(WriteIndex);

	if (VerificationLevel() >= 1)
	{
		VerifyLRU();
	}
}

void FStreamingManager::VerifyLRU()
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_VerifyLRU);

	check(RegisteredPageIndexToLRU.Num() == MaxStreamingPages);
	check(LRUToRegisteredPageIndex.Num() == MaxStreamingPages);

	TBitArray<> ReferenceMap;
	ReferenceMap.Init(false, MaxStreamingPages);
	for (uint32 RegisteredPageIndex = 0; RegisteredPageIndex < MaxStreamingPages; RegisteredPageIndex++)
	{
		const uint32 LRUIndex = RegisteredPageIndexToLRU[RegisteredPageIndex];

		check(!ReferenceMap[LRUIndex]);
		ReferenceMap[LRUIndex] = true;

		check(LRUToRegisteredPageIndex[LRUIndex] == RegisteredPageIndex);
	}
}

void FStreamingManager::SelectHighestPriorityPagesAndUpdateLRU(uint32 MaxSelectedPages)
{
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_SelectHighestPriority);

	const auto StreamingRequestPriorityPredicate = [](const FStreamingRequest& A, const FStreamingRequest& B)
	{
		return A.Priority > B.Priority;
	};

	PrioritizedRequestsHeap.Reset();

	for (const FNewPageRequest& NewPageRequest : RequestedNewPages)
	{
		FStreamingRequest StreamingRequest;
		StreamingRequest.Key = NewPageRequest.Key;
		StreamingRequest.Priority = RegisteredVirtualPages[NewPageRequest.VirtualPageIndex].Priority;
			
		PrioritizedRequestsHeap.Push(StreamingRequest);
	}

	const uint32 NumNewPageRequests = PrioritizedRequestsHeap.Num();
	const uint32 NumUniqueRequests = RequestedRegisteredPages.Num() + RequestedNewPages.Num();

	SET_DWORD_STAT(STAT_NaniteStreaming27_PageRequestsNew, NumNewPageRequests);
	CSV_CUSTOM_STAT(NaniteStreamingDetail, NewStreamingDataSizeMB, NumNewPageRequests * (NANITE_STREAMING_PAGE_GPU_SIZE / 1048576.0f), ECsvCustomStatOp::Set);

	StatVisibleSetSize = NumUniqueRequests;

	StatStreamingPoolPercentage = MaxStreamingPages ? NumUniqueRequests / float(MaxStreamingPages) * 100.0f : 0.0f;
	QualityScaleFactor = QualityScalingManager->Update(StatStreamingPoolPercentage);

	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_Heapify);
		PrioritizedRequestsHeap.Heapify(StreamingRequestPriorityPredicate);
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_UpdateLRU);
		for (const uint32 VirtualPageIndex : RequestedRegisteredPages)
		{
			const uint32 RegisteredPageIndex = RegisteredVirtualPages[VirtualPageIndex].RegisteredPageIndex;
			MoveToEndOfLRUList(RegisteredPageIndex);
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_ClearReferencedArray);
		for (const uint32 VirtualPageIndex : RequestedRegisteredPages)
		{
			RegisteredVirtualPages[VirtualPageIndex].Priority = 0;
		}

		for (const FNewPageRequest& NewPageRequest : RequestedNewPages)
		{
			RegisteredVirtualPages[NewPageRequest.VirtualPageIndex].Priority = 0;
		}
	}


	if (VerificationLevel() >= 1)
	{
		for (const FRegisteredVirtualPage& Page : RegisteredVirtualPages)
		{
			check(Page.Priority == 0);
		}
	}
	
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SelectStreamingPages);
		while ((uint32)SelectedPages.Num() < MaxSelectedPages && PrioritizedRequestsHeap.Num() > 0)
		{
			FStreamingRequest SelectedRequest;
			PrioritizedRequestsHeap.HeapPop(SelectedRequest, StreamingRequestPriorityPredicate, EAllowShrinking::No);

			const FRootPageInfo* RootPageInfo = GetRootPage(SelectedRequest.Key.RuntimeResourceID);
			if (RootPageInfo && !RootPageInfo->bInvalidResource)
			{
				const FResources* Resources = RootPageInfo->Resources;

				const uint32 NumResourcePages = (uint32)Resources->PageStreamingStates.Num();
				if (SelectedRequest.Key.PageIndex < NumResourcePages)
				{
					SelectedPages.Push(SelectedRequest.Key);
				}
				else
				{
					checkf(false, TEXT("Reference to page index that is out of bounds: %d / %d. "
						"This could be caused by GPUScene corruption or issues with the GPU readback."),
						SelectedRequest.Key.PageIndex, NumResourcePages);
				}
			}
		}
		check((uint32)SelectedPages.Num() <= MaxSelectedPages);
	}
}

void FStreamingManager::AsyncUpdate()
{
	LLM_SCOPE_BYTAG(Nanite);
	SCOPED_NAMED_EVENT(FStreamingManager_AsyncUpdate, FColor::Cyan);
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::AsyncUpdate);
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_AsyncUpdate);

	check(AsyncState.bUpdateActive);
	InstallReadyPages(AsyncState.NumReadyOrSkippedPages);

	const uint32 StartTime = FPlatformTime::Cycles();


	if (AsyncState.GPUStreamingRequestsPtr)
	{
		RequestedRegisteredPages.Reset();
		RequestedNewPages.Reset();
	
		{
			SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_ProcessRequests);

			SET_DWORD_STAT(STAT_NaniteStreaming20_PageRequests, 0);

			AddPendingGPURequests();
		#if WITH_EDITOR
			RecordGPURequests();
		#endif
			AddPendingExplicitRequests();
			AddPendingResourcePrefetchRequests();
			AddParentRequests();

			SET_DWORD_STAT(STAT_NaniteStreaming25_PageRequestsUnique, RequestedRegisteredPages.Num() + RequestedNewPages.Num());
			SET_DWORD_STAT(STAT_NaniteStreaming26_PageRequestsRegistered, RequestedRegisteredPages.Num());
			SET_DWORD_STAT(STAT_NaniteStreaming27_PageRequestsNew, RequestedNewPages.Num());
		}

		// NOTE: Requests can still contain references to resources that are no longer resident.
		const uint32 MaxSelectedPages = MaxPendingPages - NumPendingPages;
		SelectedPages.Reset();
		SelectHighestPriorityPagesAndUpdateLRU(MaxSelectedPages);

		uint32 NumLegacyRequestsIssued = 0;

		if( !SelectedPages.IsEmpty() )
		{
		#if WITH_EDITOR
			TArray<FCacheGetChunkRequest> DDCRequests;
			DDCRequests.Reserve(MaxSelectedPages);
		#endif

			FBulkDataBatchRequest::FBatchBuilder Batch = FBulkDataBatchRequest::NewBatch(SelectedPages.Num());
			bool bIssueIOBatch = false;
			float TotalIORequestSizeMB = 0.0f;

			// Register Pages
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RegisterPages);

				int32 NextLRUTestIndex = 0;
				for( const FPageKey& SelectedKey : SelectedPages )
				{
					FResources* Resources = GetResources(SelectedKey.RuntimeResourceID);
					check(Resources);
					FByteBulkData& BulkData = Resources->StreamablePages;
#if WITH_EDITOR
					const bool bDiskRequest = !(Resources->ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC) && !BulkData.IsBulkDataLoaded();
#else
					const bool bDiskRequest = true;
#endif

					const bool bLegacyRequest = bDiskRequest && !BulkData.IsUsingIODispatcher();
					if (bLegacyRequest && NumLegacyRequestsIssued == MAX_LEGACY_REQUESTS_PER_UPDATE)
					{
						break;
					}

					FRegisteredPage* Page = nullptr;					
					while(NextLRUTestIndex < LRUToRegisteredPageIndex.Num())
					{
						const uint32 Entry = LRUToRegisteredPageIndex[NextLRUTestIndex++];
						if (Entry == INDEX_NONE || (Entry & LRU_FLAG_REFERENCED_THIS_UPDATE) != 0)
						{
							continue;
						}

						const uint32 RegisteredPageIndex = Entry & LRU_INDEX_MASK;
						FRegisteredPage* CandidatePage = &RegisteredPages[RegisteredPageIndex];
						if (CandidatePage && CandidatePage->RefCount == 0)
						{
							Page = CandidatePage;
							break;
						}
					}

					if (!Page)
					{
						break;	// Couldn't find a free page. Abort.
					}

					const FPageStreamingState& PageStreamingState = Resources->PageStreamingStates[SelectedKey.PageIndex];
					check(!Resources->IsRootPage(SelectedKey.PageIndex));

					FPendingPage& PendingPage = PendingPages[NextPendingPageIndex];
					PendingPage = FPendingPage();

#if WITH_EDITOR
					if (!bDiskRequest)
					{
						if (Resources->ResourceFlags & NANITE_RESOURCE_FLAG_STREAMING_DATA_IN_DDC)
						{
							DDCRequests.Add(BuildDDCRequest(*Resources, PageStreamingState, NextPendingPageIndex));
							PendingPage.State = FPendingPage::EState::DDC_Pending;
						}
						else
						{
							PendingPage.State = FPendingPage::EState::Memory;
						}
					}
					else
#endif
					{
						uint32 AllocatedOffset;
						if (!PendingPageStagingAllocator->TryAllocate(PageStreamingState.BulkSize, AllocatedOffset))
						{
							// Staging ring buffer full. Postpone any remaining pages to next frame.
							// UE_LOG(LogNaniteStreaming, Verbose, TEXT("This should be a rare event."));
							break;
						}
						TRACE_IOSTORE_METADATA_SCOPE_TAG("NaniteGPU");
						uint8* Dst = PendingPageStagingMemory.GetData() + AllocatedOffset;
						PendingPage.RequestBuffer = FIoBuffer(FIoBuffer::Wrap, Dst, PageStreamingState.BulkSize);
						PendingPage.RingBufferAllocationSize = PageStreamingState.BulkSize;
						Batch.Read(BulkData, PageStreamingState.BulkOffset, PageStreamingState.BulkSize, AIOP_Low, PendingPage.RequestBuffer, PendingPage.Request);
						bIssueIOBatch = true;

						if (bLegacyRequest)
						{
							NumLegacyRequestsIssued++;
						}
#if WITH_EDITOR
						PendingPage.State = FPendingPage::EState::Disk;
#endif
					}

					UnregisterStreamingPage(Page->Key);

					TotalIORequestSizeMB += PageStreamingState.BulkSize * (1.0f / 1048576.0f);

					PendingPage.InstallKey = SelectedKey;
					const uint32 GPUPageIndex = uint32(Page - RegisteredPages.GetData());
					PendingPage.GPUPageIndex = GPUPageIndex;

					NextPendingPageIndex = (NextPendingPageIndex + 1) % MaxPendingPages;
					NumPendingPages++;

					PendingPage.BytesLeftToStream = PageStreamingState.BulkSize;

					RegisterStreamingPage(GPUPageIndex, SelectedKey);
				}
			}

			INC_FLOAT_STAT_BY(STAT_NaniteStreaming40_IORequestSizeMB, TotalIORequestSizeMB);

			CSV_CUSTOM_STAT(NaniteStreamingDetail, IORequestSizeMB, TotalIORequestSizeMB, ECsvCustomStatOp::Set);
			CSV_CUSTOM_STAT(NaniteStreamingDetail, IORequestSizeMBps, TotalIORequestSizeMB / FPlatformTime::ToSeconds(StartTime - StatPrevUpdateTime), ECsvCustomStatOp::Set);

#if WITH_EDITOR
			if (DDCRequests.Num() > 0)
			{
				RequestDDCData(DDCRequests);
				DDCRequests.Empty();
			}
#endif

			if (bIssueIOBatch)
			{
				// Issue batch
				TRACE_CPUPROFILER_EVENT_SCOPE(FIoBatch::Issue);
				(void)Batch.Issue();
			}
		}

		CompactLRU();

#if !WITH_EDITOR
		// Issue warning if we end up taking the legacy path
		static const bool bUsingPakFiles = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")) != nullptr;
		if (NumLegacyRequestsIssued > 0 && bUsingPakFiles)
		{
			static bool bHasWarned = false;
			if (!bHasWarned)
			{
				UE_LOG(LogNaniteStreaming, Warning, TEXT("PERFORMANCE WARNING: Nanite is issuing IO requests using the legacy IO path. Expect slower streaming and higher CPU overhead. "
					"To avoid this penalty make sure iostore is enabled, it is supported by the platform, and that resources are built with -iostore."));
				bHasWarned = true;
			}
		}
#endif
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_ResolveOverwrites);
		const bool bVerify = VerificationLevel() >= 1;
		ClusterScatterUpdates->ResolveOverwrites(bVerify);
		HierarchyScatterUpdates->ResolveOverwrites(bVerify);
	}
	
	if (VerificationLevel() >= 2)
	{
		VerifyFixupState();
	}

	StatPrevUpdateTime = StartTime;
	CSV_CUSTOM_STAT(NaniteStreamingDetail, AsyncUpdateMs, 1000.0f * FPlatformTime::ToSeconds(FPlatformTime::Cycles() - StartTime), ECsvCustomStatOp::Set);

}

void FStreamingManager::EndAsyncUpdate(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	if (!DoesPlatformSupportNanite(GMaxRHIShaderPlatform))
	{
		return;
	}

	LLM_SCOPE_BYTAG(Nanite);
	TRACE_CPUPROFILER_EVENT_SCOPE(FStreamingManager::EndAsyncUpdate);
	
	RDG_EVENT_SCOPE_STAT(GraphBuilder, NaniteStreaming, "Nanite::EndAsyncUpdate");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteStreaming);

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());
	SCOPE_CYCLE_COUNTER(STAT_NaniteStreaming_EndAsyncUpdate);

	check(AsyncState.bUpdateActive);

	// Wait for async processing to finish
	if (AsyncTaskEvents.Num() > 0)
	{
		FTaskGraphInterface::Get().WaitUntilTasksComplete(AsyncTaskEvents, ENamedThreads::GetRenderThread_Local());
	}

	AsyncTaskEvents.Empty();

	if (AsyncState.GPUStreamingRequestsPtr)
	{
		ReadbackManager->Unlock();
	}

	// Issue GPU copy operations
	if (AsyncState.NumReadyOrSkippedPages > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UploadPages);

		const FRDGBufferRef ClusterPageDataBuffer = GraphBuilder.RegisterExternalBuffer(ClusterPageData.DataBuffer);
		PageUploader->ResourceUploadTo(GraphBuilder, ClusterPageDataBuffer);

		ClusterScatterUpdates->Flush(GraphBuilder, GraphBuilder.CreateUAV(ClusterPageDataBuffer));
		HierarchyScatterUpdates->Flush(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(Hierarchy.DataBuffer)));

	#if !DEBUG_TRANSCODE_PAGES_REPEATEDLY
		NumPendingPages -= AsyncState.NumReadyOrSkippedPages;
	#endif
	}

	MaxHierarchyLevels = HierarchyDepthManager->CalculateNumLevels();
	SET_DWORD_STAT(STAT_NaniteStreaming04_MaxHierarchyLevels, MaxHierarchyLevels);

	CSV_CUSTOM_STAT(NaniteStreamingDetail, StreamingPoolSizeMB, MaxStreamingPages * (NANITE_STREAMING_PAGE_GPU_SIZE / 1048576.0f), ECsvCustomStatOp::Set);

	const float VisibleStreamingDataSizeMB = StatVisibleSetSize * (NANITE_STREAMING_PAGE_GPU_SIZE / 1048576.0f);
	SET_FLOAT_STAT(STAT_NaniteStreaming30_VisibleStreamingDataSizeMB, VisibleStreamingDataSizeMB);
	CSV_CUSTOM_STAT(NaniteStreamingDetail, VisibleStreamingDataSizeMB, VisibleStreamingDataSizeMB, ECsvCustomStatOp::Set);
	
	SET_FLOAT_STAT(STAT_NaniteStreaming31_VisibleStreamingPoolPercentage, StatStreamingPoolPercentage);
	SET_FLOAT_STAT(STAT_NaniteStreaming32_VisibleStreamingQualityScale, QualityScaleFactor);

	AsyncState.bUpdateActive = false;
}

void FStreamingManager::SubmitFrameStreamingRequests(FRDGBuilder& GraphBuilder)
{
}

bool FStreamingManager::IsAsyncUpdateInProgress()
{
	return AsyncState.bUpdateActive;
}

bool FStreamingManager::IsSafeForRendering() const
{
	return !AsyncState.bUpdateActive && PendingAdds.Num() == 0;
}

void FStreamingManager::PrefetchResource(const FResources* Resources, uint32 NumFramesUntilRender)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	check(Resources);
	if (GNaniteStreamingPrefetch)
	{
		FResourcePrefetch Prefetch;
		Prefetch.RuntimeResourceID		= Resources->RuntimeResourceID;
		Prefetch.NumFramesUntilRender	= FMath::Min(NumFramesUntilRender, 30u);		// Make sure invalid values doesn't cause the request to stick around forever
		PendingResourcePrefetches.Add(Prefetch);
	}
}

void FStreamingManager::RequestNanitePages(TArrayView<uint32> RequestData)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	if (GNaniteStreamingExplicitRequests)
	{
		PendingExplicitRequests.Append(RequestData.GetData(), RequestData.Num());
	}
}

uint32 FStreamingManager::GetStreamingRequestsBufferVersion() const
{
	return ReadbackManager->GetBufferVersion();
}

#if WITH_EDITOR
uint64 FStreamingManager::GetRequestRecordBuffer(TArray<uint32>& OutRequestData)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	if (PageRequestRecordHandle == (uint64)-1)
	{
		return (uint64)-1;
	}

	const uint64 Ret = PageRequestRecordHandle;
	PageRequestRecordHandle = (uint64)-1;
	if (PageRequestRecordMap.Num() == 0)
	{
		OutRequestData.Empty();
		return Ret;
	}

	// Resolve requests and convert to persistent resource IDs
	TArray<FStreamingRequest> Requests;
	Requests.Reserve(PageRequestRecordMap.Num());
	for (const TPair<FPageKey, uint32>& MapEntry : PageRequestRecordMap)
	{
		FResources* Resources = GetResources(MapEntry.Key.RuntimeResourceID);
		if (Resources)
		{	
			Requests.Add(FStreamingRequest { FPageKey { Resources->PersistentHash, MapEntry.Key.PageIndex }, MapEntry.Value } );
		}
	}
	PageRequestRecordMap.Reset();

	Requests.Sort();

	// Count unique resources
	uint32 NumUniqueResources = 0;
	{
		uint64 PrevPersistentHash = NANITE_INVALID_PERSISTENT_HASH;
		for (const FStreamingRequest& Request : Requests)
		{
			if (Request.Key.RuntimeResourceID != PrevPersistentHash)
			{
				NumUniqueResources++;
			}
			PrevPersistentHash = Request.Key.RuntimeResourceID;
		}
	}
	
	// Write packed requests
	// A request consists of two DWORDs. A resource DWORD and a pageindex/priority/repeat DWORD.
	// The repeat bit indicates if the next request is to the same resource, so the resource DWORD can be omitted.
	// As there are often many requests per resource, this encoding can safe upwards of half of the total DWORDs.
	{
		const uint32 NumOutputDwords = NumUniqueResources + Requests.Num();
		OutRequestData.SetNum(NumOutputDwords);
		uint32 WriteIndex = 0;
		uint64 PrevResourceID = ~0ull;
		for (const FStreamingRequest& Request : Requests)
		{
			check(Request.Key.PageIndex < NANITE_MAX_RESOURCE_PAGES);
			if (Request.Key.RuntimeResourceID != PrevResourceID)
			{
				OutRequestData[WriteIndex++] = Request.Key.RuntimeResourceID;
			}
			else
			{
				OutRequestData[WriteIndex - 1] |= 1;	// Mark resource repeat bit in previous packed dword
 			}
			PrevResourceID = Request.Key.RuntimeResourceID;

			const uint32 QuantizedPriority = Request.Priority >> (NANITE_MAX_RESOURCE_PAGES_BITS + 1);	// Exact priority doesn't matter, so just quantize it to fit
			const uint32 Packed = (QuantizedPriority << (NANITE_MAX_RESOURCE_PAGES_BITS + 1)) | (Request.Key.PageIndex << 1);	// Lowest bit is resource repeat bit
			OutRequestData[WriteIndex++] = Packed;
		}

		check(WriteIndex == NumOutputDwords);
	}
	
	return Ret;
}

void FStreamingManager::SetRequestRecordBuffer(uint64 Handle)
{
	check(IsInRenderingThread());
	check(!AsyncState.bUpdateActive);
	PageRequestRecordHandle = Handle;
	PageRequestRecordMap.Empty();
}

void FStreamingManager::RecordGPURequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RecordGPURequests);
	if (PageRequestRecordHandle != (uint64)-1)
	{
		auto UpdateKeyPriority = [this](const FPageKey& Key, uint32 Priority)
		{
			uint32* PriorityPtr = PageRequestRecordMap.Find(Key);
			if (PriorityPtr)
				*PriorityPtr = FMath::Max(*PriorityPtr, Priority);
			else
				PageRequestRecordMap.Add(Key, Priority);
		};

		for (uint32 VirtualPageIndex : RequestedRegisteredPages)
		{
			const FRegisteredVirtualPage& VirtualPage = RegisteredVirtualPages[VirtualPageIndex];
			const FRegisteredPage& RegisteredPage = RegisteredPages[VirtualPage.RegisteredPageIndex];
			UpdateKeyPriority(RegisteredPage.Key, VirtualPage.Priority);
		}

		for (const FNewPageRequest& Request : RequestedNewPages)
		{
			const FRegisteredVirtualPage& VirtualPage = RegisteredVirtualPages[Request.VirtualPageIndex];
			UpdateKeyPriority(Request.Key, VirtualPage.Priority);
		}
	}
}

FCacheGetChunkRequest FStreamingManager::BuildDDCRequest(const FResources& Resources, const FPageStreamingState& PageStreamingState, const uint32 PendingPageIndex)
{
	FCacheKey Key;
	Key.Bucket = FCacheBucket(TEXT("StaticMesh"));
	Key.Hash = Resources.DDCKeyHash;
	check(!Resources.DDCRawHash.IsZero());

	FCacheGetChunkRequest Request;
	Request.Id			= NaniteValueId;
	Request.Key			= Key;
	Request.RawOffset	= PageStreamingState.BulkOffset;
	Request.RawSize		= PageStreamingState.BulkSize;
	Request.RawHash		= Resources.DDCRawHash;
	Request.UserData	= PendingPageIndex;
	return Request;
}

void FStreamingManager::RequestDDCData(TConstArrayView<FCacheGetChunkRequest> DDCRequests)
{
	FRequestBarrier Barrier(*RequestOwner);	// This is a critical section on the owner. It does not constrain ordering
	GetCache().GetChunks(DDCRequests, *RequestOwner,
		[this](FCacheGetChunkResponse&& Response)
		{
			const uint32 PendingPageIndex = (uint32)Response.UserData;
			FPendingPage& PendingPage = PendingPages[PendingPageIndex];

			if (Response.Status == EStatus::Ok)
			{
				PendingPage.SharedBuffer = MoveTemp(Response.RawData);
				PendingPage.State = FPendingPage::EState::DDC_Ready;
			}
			else
			{
				PendingPage.State = FPendingPage::EState::DDC_Failed;
			}
		});
}

#endif // WITH_EDITOR

TGlobalResource< FStreamingManager > GStreamingManager;

} // namespace Nanite
