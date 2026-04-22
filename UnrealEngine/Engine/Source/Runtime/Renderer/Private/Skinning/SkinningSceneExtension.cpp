// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinningSceneExtension.h"
#include "ViewDefinitions.h"
#include "ScenePrivate.h"
#include "RenderUtils.h"
#include "SkeletalRenderPublic.h"
#include "SkinningDefinitions.h"
#include "ViewData.h"
#include "SceneCulling/SceneCullingRenderer.h"
#include "UnifiedBuffer.h"
#include "Animation/Skeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"

static int32 GSkinningBuffersTransformDataMinSizeBytes = 4 * 1024;
static FAutoConsoleVariableRef CVarSkinningBuffersTransformDataMinSizeBytes(
	TEXT("r.Skinning.Buffers.TransformDataMinSizeBytes"),
	GSkinningBuffersTransformDataMinSizeBytes,
	TEXT("The smallest size (in bytes) of the bone transform data buffer."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static int32 GSkinningBuffersHeaderDataMinSizeBytes = 4 * 1024;
static FAutoConsoleVariableRef CVarSkinningBuffersHeaderDataMinSizeBytes(
	TEXT("r.Skinning.Buffers.HeaderDataMinSizeBytes"),
	GSkinningBuffersHeaderDataMinSizeBytes,
	TEXT("The smallest size (in bytes) of the per-primitive skinning header data buffer."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe
);

static bool GSkinningBuffersAsyncUpdate = true;
static FAutoConsoleVariableRef CVarSkinningBuffersAsyncUpdates(
	TEXT("r.Skinning.Buffers.AsyncUpdate"),
	GSkinningBuffersAsyncUpdate,
	TEXT("When non-zero, skinning data buffer updates are updated asynchronously."),
	ECVF_RenderThreadSafe
);

static int32 GSkinningBuffersForceFullUpload = 0;
static FAutoConsoleVariableRef CVarSkinningBuffersForceFullUpload(
	TEXT("r.Skinning.Buffers.ForceFullUpload"),
	GSkinningBuffersForceFullUpload,
	TEXT("0: Do not force a full upload.\n")
	TEXT("1: Force one full upload on the next update.\n")
	TEXT("2: Force a full upload every frame."),
	ECVF_RenderThreadSafe
);

static bool GSkinningBuffersDefrag = true;
static FAutoConsoleVariableRef CVarSkinningBuffersDefrag(
	TEXT("r.Skinning.Buffers.Defrag"),
	GSkinningBuffersDefrag,
	TEXT("Whether or not to allow defragmentation of the skinning buffers."),
	ECVF_RenderThreadSafe
);

static int32 GSkinningBuffersForceDefrag = 0;
static FAutoConsoleVariableRef CVarSkinningBuffersForceDefrag(
	TEXT("r.Skinning.Buffers.Defrag.Force"),
	GSkinningBuffersForceDefrag,
	TEXT("0: Do not force a full defrag.\n")
	TEXT("1: Force one full defrag on the next update.\n")
	TEXT("2: Force a full defrag every frame."),
	ECVF_RenderThreadSafe
);

static float GSkinningBuffersDefragLowWatermark = 0.375f;
static FAutoConsoleVariableRef CVarSkinningBuffersDefragLowWatermark(
	TEXT("r.Skinning.Buffers.Defrag.LowWatermark"),
	GSkinningBuffersDefragLowWatermark,
	TEXT("Ratio of used to allocated memory at which to decide to defrag the skinning buffers."),
	ECVF_RenderThreadSafe
);

static bool GSkinningTransformProviders = true;
static FAutoConsoleVariableRef CVarSkinningTransformProviders(
	TEXT("r.Skinning.TransformProviders"),
	GSkinningTransformProviders,
	TEXT("When set, transform providers are enabled (if registered)."),
	ECVF_RenderThreadSafe
);

static float GSkinningDefaultAnimationMinScreenSize = 0.1f;
static FAutoConsoleVariableRef CVarSkinningDefaultAnimationMinScreenSize(
	TEXT("r.Skinning.DefaultAnimationMinScreenSize"),
	GSkinningDefaultAnimationMinScreenSize,
	TEXT("Default animation screen size to stop animating at, applies when the per-component value is 0.0."),
	ECVF_RenderThreadSafe
);

BEGIN_UNIFORM_BUFFER_STRUCT(FSkinningSceneParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, Headers)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, BoneHierarchy)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, BoneObjectSpace)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, BoneTransforms)
END_UNIFORM_BUFFER_STRUCT()

DECLARE_SCENE_UB_STRUCT(FSkinningSceneParameters, Skinning, RENDERER_API)

// Reference pose transform provider
struct FTransformBlockHeader
{
	uint32 BlockLocalIndex;
	uint32 BlockTransformCount;
	uint32 BlockTransformOffset;
};

class FRefPoseTransformProviderCS : public FGlobalShader
{
public:
	static constexpr uint32 TransformsPerGroup = 64u;

private:
	DECLARE_GLOBAL_SHADER(FRefPoseTransformProviderCS);
	SHADER_USE_PARAMETER_STRUCT(FRefPoseTransformProviderCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, TransformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FTransformBlockHeader>, HeaderBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
#if USE_SKINNING_SCENE_EXTENSION_FOR_NON_NANITE
		return true;
#else
		return DoesPlatformSupportNanite(Parameters.Platform);
#endif
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);

		OutEnvironment.SetDefine(TEXT("TRANSFORMS_PER_GROUP"), TransformsPerGroup);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRefPoseTransformProviderCS, "/Engine/Private/Skinning/TransformProviders.usf", "RefPoseProviderCS", SF_Compute);

static FGuid RefPoseProviderId(REF_POSE_TRANSFORM_PROVIDER_GUID);
static FGuid AnimRuntimeProviderId(ANIM_RUNTIME_TRANSFORM_PROVIDER_GUID);

static void GetDefaultSkinningParameters(FSkinningSceneParameters& OutParameters, FRDGBuilder& GraphBuilder)
{
	auto DefaultBuffer = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultByteAddressBuffer(GraphBuilder, 4u));
	OutParameters.Headers			= DefaultBuffer;
	OutParameters.BoneHierarchy		= DefaultBuffer;
	OutParameters.BoneObjectSpace	= DefaultBuffer;
	OutParameters.BoneTransforms	= DefaultBuffer;
}

IMPLEMENT_SCENE_EXTENSION(FSkinningSceneExtension);

bool FSkinningSceneExtension::ShouldCreateExtension(FScene& InScene)
{
#if USE_SKINNING_SCENE_EXTENSION_FOR_NON_NANITE
	return true;
#else
	return NaniteSkinnedMeshesSupported() && DoesRuntimeSupportNanite(GetFeatureLevelShaderPlatform(InScene.GetFeatureLevel()), true, true);
#endif
}

FSkinningSceneExtension::FSkinningSceneExtension(FScene& InScene)
:	ISceneExtension(InScene)
{
	WorldRef = InScene.GetWorld();
	UpdateTimerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FSkinningSceneExtension::Tick));
}

FSkinningSceneExtension::~FSkinningSceneExtension()
{
	FTSTicker::RemoveTicker(UpdateTimerHandle);
}

void FSkinningSceneExtension::InitExtension(FScene& InScene)
{
	// Register animation runtime and reference pose transform providers
	if (auto TransformProvider = Scene.GetExtensionPtr<FSkinningTransformProvider>())
	{
		TransformProvider->RegisterProvider(
			GetRefPoseProviderId(),
			FSkinningTransformProvider::FOnProvideTransforms::CreateStatic(&FSkinningSceneExtension::ProvideRefPoseTransforms),
			false /* Use skeleton batching */
		);

		TransformProvider->RegisterProvider(
			GetAnimRuntimeProviderId(),
			FSkinningTransformProvider::FOnProvideTransforms::CreateStatic(&FSkinningSceneExtension::ProvideAnimRuntimeTransforms),
			false /* Use skeleton batching */
		);

#if USE_SKINNING_SCENE_EXTENSION_FOR_NON_NANITE
		SetEnabled(true);
#else
		const bool bNaniteEnabled = UseNanite(GetFeatureLevelShaderPlatform(InScene.GetFeatureLevel()));
		SetEnabled(bNaniteEnabled);
#endif
	}
}

ISceneExtensionUpdater* FSkinningSceneExtension::CreateUpdater()
{
	return new FUpdater(*this);
}

ISceneExtensionRenderer* FSkinningSceneExtension::CreateRenderer(FSceneRendererBase& InSceneRenderer, const FEngineShowFlags& EngineShowFlags)
{
	// We only need to create renderers when we're enabled
	if (!IsEnabled())
	{
		return nullptr;
	}

	return new FRenderer(InSceneRenderer, *this);
}

void FSkinningSceneExtension::SetEnabled(bool bEnabled)
{
	if (bEnabled != IsEnabled())
	{
		if (bEnabled)
		{
			Buffers = MakeUnique<FBuffers>();
		}
		else
		{
			Buffers = nullptr;
			HierarchyAllocator.Reset();
			TransformAllocator.Reset();
			HeaderData.Reset();
			BatchHeaderData.Reset();
			HeaderDataIndices.Reset();
		}
	}
}

void FSkinningSceneExtension::FinishSkinningBufferUpload(FRDGBuilder& GraphBuilder, FSkinningSceneParameters* OutParams)
{
	if (!IsEnabled())
	{
		return;
	}

	FRDGBufferRef HeaderBuffer = nullptr;
	FRDGBufferRef BoneHierarchyBuffer = nullptr;
	FRDGBufferRef BoneObjectSpaceBuffer = nullptr;
	FRDGBufferRef TransformBuffer = nullptr;

	// Sync on upload tasks
	UE::Tasks::Wait(
		MakeArrayView(
			{
				TaskHandles[UploadHeaderDataTask],
				TaskHandles[UploadHierarchyDataTask],
				TaskHandles[UploadTransformDataTask]
			}
		)
	);

	const uint32 MinHeaderDataSize = (HeaderData.GetMaxIndex() + 1);
	const uint32 MinTransformDataSize = TransformAllocator.GetMaxSize();
	const uint32 MinHierarchyDataSize = HierarchyAllocator.GetMaxSize();
	const uint32 MinObjectSpaceDataSize = ObjectSpaceAllocator.GetMaxSize();

	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	if (Uploader.IsValid())
	{
		HeaderBuffer = Uploader->HeaderDataUploader.ResizeAndUploadTo(
			GraphBuilder,
			Buffers->HeaderDataBuffer,
			MinHeaderDataSize
		);

		BoneHierarchyBuffer = Uploader->BoneHierarchyUploader.ResizeAndUploadTo(
			GraphBuilder,
			Buffers->BoneHierarchyBuffer,
			MinHierarchyDataSize
		);

		BoneObjectSpaceBuffer = Uploader->BoneObjectSpaceUploader.ResizeAndUploadTo(
			GraphBuilder,
			Buffers->BoneObjectSpaceBuffer,
			MinObjectSpaceDataSize
		);

		TransformBuffer = Uploader->TransformDataUploader.ResizeAndUploadTo(
			GraphBuilder,
			Buffers->TransformDataBuffer,
			MinTransformDataSize
		);

		Uploader = nullptr;
	}
	else
	{
		HeaderBuffer			= Buffers->HeaderDataBuffer.ResizeBufferIfNeeded(GraphBuilder, MinHeaderDataSize);
		BoneHierarchyBuffer		= Buffers->BoneHierarchyBuffer.ResizeBufferIfNeeded(GraphBuilder, MinHierarchyDataSize);
		BoneObjectSpaceBuffer	= Buffers->BoneObjectSpaceBuffer.ResizeBufferIfNeeded(GraphBuilder, MinObjectSpaceDataSize);
		TransformBuffer			= Buffers->TransformDataBuffer.ResizeBufferIfNeeded(GraphBuilder, MinTransformDataSize);
	}

	if (OutParams != nullptr)
	{
		OutParams->Headers			= GraphBuilder.CreateSRV(HeaderBuffer);
		OutParams->BoneHierarchy	= GraphBuilder.CreateSRV(BoneHierarchyBuffer);
		OutParams->BoneObjectSpace	= GraphBuilder.CreateSRV(BoneObjectSpaceBuffer);
		OutParams->BoneTransforms	= GraphBuilder.CreateSRV(TransformBuffer);
	}
}

void FSkinningSceneExtension::PerformSkinning(FSkinningSceneParameters& Parameters, FRDGBuilder& GraphBuilder)
{
	RDG_EVENT_SCOPE(GraphBuilder, "Skinning");

	const float CurrentDeltaTime = TickState->DeltaTime;
	TickState->DeltaTime = 0.0f;

	if (!GSkinningTransformProviders)
	{
		return;
	}

	if (auto TransformProvider = Scene.GetExtensionPtr<FSkinningTransformProvider>())
	{
		if (HeaderData.Num() == 0)
		{
			return;
		}

		const TArray<FGuid> SkeletonProviderIds = TransformProvider->GetSkeletonProviderIds();
		const TArray<FGuid> PrimitiveProviderIds = TransformProvider->GetPrimitiveProviderIds();

		checkf((SkeletonProviderIds.Num() + PrimitiveProviderIds.Num()) < 256, TEXT("The number of provider ids exceeds storage capacity for PrimitivesToRangeIndex."));

		auto ResetRanges = [](const TArray<FGuid>& Providers, TArray<FSkinningTransformProvider::FProviderRange, TInlineAllocator<8>>& Ranges)
		{
			Ranges.Reset();
			for (const FGuid& ProviderId : Providers)
			{
				FSkinningTransformProvider::FProviderRange& Range = Ranges.Emplace_GetRef();
				Range.Id = ProviderId;
				Range.Count = 0;
				Range.Offset = 0;
			}
		};

		// TODO: Optimize further (incremental tracking of primitives within provider extension?)
		// The current assumption is that skinned primitive counts should be fairly low, and heavy
		// instancing would be used. If we need a ton of primitives, revisit this algorithm.

		struct FOffsets
		{
			uint32 TransformOffset;
			uint32 HierarchyOffset;
		};

		// Skeleton
		if (BatchHeaderData.Num() > 0)
		{
			TArray<FSkinningTransformProvider::FProviderRange, TInlineAllocator<8>> SkeletonRanges;
			SkeletonRanges.Reserve(SkeletonProviderIds.Num());
			ResetRanges(SkeletonProviderIds, SkeletonRanges);

			TArrayView<FSkeletonBatch> Batches = GraphBuilder.AllocPODArrayView<FSkeletonBatch>(BatchHeaderData.Num());
			TArrayView<FOffsets> Offsets = GraphBuilder.AllocPODArrayView<FOffsets>(BatchHeaderData.Num());

			uint32 TotalOffset = 0;
			uint32 TotalBatches = 0;

			for (auto& [BatchKey, Header] : BatchHeaderData)
			{
				const FGuid ProviderId = BatchKey.TransformProviderId;
				for (FSkinningTransformProvider::FProviderRange& Range : SkeletonRanges)
				{
					if (ProviderId == Range.Id)
					{
						++Range.Count;
						break;
					}
				}

				Batches[TotalBatches] = FSkeletonBatch
				{
				#if ENABLE_SKELETON_DEBUG_NAME
					.SkeletonName = BatchKey.SkeletonName,
				#endif
					.SkeletonGuid = BatchKey.SkeletonGuid,
					.MaxBoneTransforms = Header.MaxTransformCount,
					.UniqueAnimationCount = Header.UniqueAnimationCount
				};

				Offsets[TotalBatches].TransformOffset = Header.TransformBufferOffset;
				Offsets[TotalBatches].HierarchyOffset = Header.HierarchyBufferOffset;

				++TotalBatches;
			}

			uint32 IndirectionCount = 0;

			for (FSkinningTransformProvider::FProviderRange& Range : SkeletonRanges)
			{
				Range.Offset = IndirectionCount;
				IndirectionCount += Range.Count;
				Range.Count = 0;
			}

			uint32 TotalBatchIndices = 0;

			TArrayView<FSkinningTransformProvider::FProviderIndirection> BatchIndices = GraphBuilder.AllocPODArrayView<FSkinningTransformProvider::FProviderIndirection>(IndirectionCount);
			for (auto& [HeaderDataCacheKey, Header] : BatchHeaderData)
			{
				const FGuid ProviderId = HeaderDataCacheKey.TransformProviderId;

				for (FSkinningTransformProvider::FProviderRange& Range : SkeletonRanges)
				{
					if (ProviderId == Range.Id)
					{
						BatchIndices[Range.Offset + Range.Count] = FSkinningTransformProvider::FProviderIndirection(
							TotalBatchIndices,
							Offsets[TotalBatchIndices].TransformOffset * sizeof(FCompressedBoneTransform),
							Offsets[TotalBatchIndices].HierarchyOffset * sizeof(uint32)
						);
						++Range.Count;
						break;
					}
				}

				++TotalBatchIndices;
			}

			if (!ensure(TotalBatches == TotalBatchIndices))
			{
				return;
			}

			FSkinningTransformProvider::FProviderContext Context(
				TConstArrayView<FPrimitiveSceneInfo*>{},
				TConstArrayView<FSkinningSceneExtensionProxy*>{},
				BatchIndices,
				Batches,
				CurrentDeltaTime,
				GraphBuilder,
				Parameters.BoneTransforms->GetParent(),
				Parameters.BoneHierarchy
			);

			TransformProvider->Broadcast(SkeletonRanges, Context);
		}

		// Primitive
		if (HeaderDataIndices.Num() > 0)
		{
			TArray<uint8, FConcurrentLinearArrayAllocator> PrimitivesToRangeIndex;
			PrimitivesToRangeIndex.AddUninitialized(HeaderData.Num());

			TArray<FSkinningTransformProvider::FProviderRange, TInlineAllocator<8>> PrimitiveRanges;
			PrimitiveRanges.Reserve(PrimitiveProviderIds.Num());
			ResetRanges(PrimitiveProviderIds, PrimitiveRanges);

			TArrayView<FPrimitiveSceneInfo*> Primitives = GraphBuilder.AllocPODArrayView<FPrimitiveSceneInfo*>(HeaderDataIndices.Num());
			TArrayView<FSkinningSceneExtensionProxy*> Proxies = GraphBuilder.AllocPODArrayView<FSkinningSceneExtensionProxy*>(HeaderDataIndices.Num());
			TArrayView<FOffsets> Offsets = GraphBuilder.AllocPODArrayView<FOffsets>(HeaderDataIndices.Num());

			uint32 TotalOffset = 0;

			uint32 PrimitiveCount = 0;
			for (const int32 HeaderDataIndex : HeaderDataIndices)
			{
				const FHeaderData& Header = HeaderData[HeaderDataIndex];
				int32 RangeIndex = 0;

				for (; RangeIndex < PrimitiveRanges.Num(); ++RangeIndex)
				{
					FSkinningTransformProvider::FProviderRange& Range = PrimitiveRanges[RangeIndex];

					if (Header.ProviderId == Range.Id)
					{
						++Range.Count;
						break;
					}
				}

				check(RangeIndex != PrimitiveRanges.Num());

				PrimitivesToRangeIndex[PrimitiveCount] = RangeIndex;
				Primitives[PrimitiveCount] = Header.PrimitiveSceneInfo;
				Proxies[PrimitiveCount] = Header.Proxy;
				Offsets[PrimitiveCount].TransformOffset = Header.TransformBufferOffset;
				Offsets[PrimitiveCount].HierarchyOffset = Header.HierarchyBufferOffset;

				++PrimitiveCount;
			}

			uint32 IndirectionCount = 0;

			for (FSkinningTransformProvider::FProviderRange& Range : PrimitiveRanges)
			{
				Range.Offset = IndirectionCount;
				IndirectionCount += Range.Count;
				Range.Count = 0;
			}

			TArrayView<FSkinningTransformProvider::FProviderIndirection> PrimitiveIndices = GraphBuilder.AllocPODArrayView<FSkinningTransformProvider::FProviderIndirection>(IndirectionCount);
			for (uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
			{
				FSkinningTransformProvider::FProviderRange& Range = PrimitiveRanges[PrimitivesToRangeIndex[PrimitiveIndex]];
				PrimitiveIndices[Range.Offset + Range.Count] = FSkinningTransformProvider::FProviderIndirection(
					PrimitiveIndex,
					Offsets[PrimitiveIndex].TransformOffset * sizeof(FCompressedBoneTransform),
					Offsets[PrimitiveIndex].HierarchyOffset * sizeof(uint32)
				);
				++Range.Count;
			}

			FSkinningTransformProvider::FProviderContext Context(
				Primitives,
				Proxies,
				PrimitiveIndices,
				TConstArrayView<FSkeletonBatch>(),
				CurrentDeltaTime,
				GraphBuilder,
				Parameters.BoneTransforms->GetParent(),
				Parameters.BoneHierarchy
			);

			TransformProvider->Broadcast(PrimitiveRanges, Context);
		}
	}
}

bool FSkinningSceneExtension::ProcessBufferDefragmentation()
{
	// Consolidate spans
	ObjectSpaceAllocator.Consolidate();
	HierarchyAllocator.Consolidate();
	TransformAllocator.Consolidate();

	// Decide to defragment the buffer when the used size dips below a certain multiple of the max used size.
	// Since the buffer allocates in powers of two, we pick the mid point between 1/4 and 1/2 in hopes to prevent
	// thrashing when usage is close to a power of 2.
	//
	// NOTES:
	//	* We only currently use the state of the transform buffer's fragmentation to decide to defrag all buffers
	//	* Rather than trying to minimize number of moves/uploads, we just realloc and re-upload everything. This
	//	  could be implemented in a more efficient manner if the current method proves expensive.

	const bool bAllowDefrag = GSkinningBuffersDefrag;
	static const int32 MinTransformBufferCount = GSkinningBuffersTransformDataMinSizeBytes / sizeof(FCompressedBoneTransform);
	const float LowWaterMarkRatio = GSkinningBuffersDefragLowWatermark;
	const int32 EffectiveMaxSize = FMath::RoundUpToPowerOfTwo(TransformAllocator.GetMaxSize());
	const int32 LowWaterMark = uint32(EffectiveMaxSize * LowWaterMarkRatio);
	const int32 UsedSize = TransformAllocator.GetSparselyAllocatedSize();
	
	if (!bAllowDefrag)
	{
		return false;
	}

	// Check to force a defrag
	const bool bForceDefrag = GSkinningBuffersForceDefrag != 0;
	if (GSkinningBuffersForceDefrag == 1)
	{
		GSkinningBuffersForceDefrag = 0;
	}
	
	if (!bForceDefrag && (EffectiveMaxSize <= MinTransformBufferCount || UsedSize > LowWaterMark))
	{
		// No need to defragment
		return false;
	}

	ObjectSpaceAllocator.Reset();
	HierarchyAllocator.Reset();
	TransformAllocator.Reset();
	BatchHeaderData.Reset();
	HeaderDataIndices.Reset();

	for (auto& Data : HeaderData)
	{
		if (Data.TransformBufferOffset != INDEX_NONE)
		{
			Data.TransformBufferOffset = INDEX_NONE;
			Data.TransformBufferCount = 0;
		}

		if (Data.HierarchyBufferOffset != INDEX_NONE)
		{
			Data.HierarchyBufferOffset = INDEX_NONE;
			Data.HierarchyBufferCount = 0;
		}

		if (Data.ObjectSpaceBufferOffset != INDEX_NONE)
		{
			Data.ObjectSpaceBufferOffset = INDEX_NONE;
			Data.ObjectSpaceBufferCount = 0;
		}
	}

	return true;
}

bool FSkinningSceneExtension::Tick(float InDeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkinningSceneExtension::Tick);

	FVector NewCameraLocation = FVector::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		if (auto PlayerController = World->GetFirstPlayerController<APlayerController>())
		{
			FRotator CameraRotation;
			PlayerController->GetPlayerViewPoint(NewCameraLocation, CameraRotation);
		}
		else
		{
			FVector LocationSum = FVector::Zero();
			if (World->ViewLocationsRenderedLastFrame.Num() > 0)
			{
				for (const auto& Location : World->ViewLocationsRenderedLastFrame)
				{
					LocationSum += Location;
				}

				NewCameraLocation = LocationSum / World->ViewLocationsRenderedLastFrame.Num();
			}
		}
	}

	// Takes a reference to keep the timer around since the update happens on the GT timeline.
	ENQUEUE_RENDER_COMMAND(FTickSkinningSceneExtension)
	([TickState = TickState, InDeltaTime, NewCameraLocation](FRHICommandListImmediate& RHICmdList)
	{
		TickState->DeltaTime += InDeltaTime;
		TickState->CameraLocation = NewCameraLocation;
	});
	return true;
}

UWorld* FSkinningSceneExtension::GetWorld() const
{
	return WorldRef.Get();
}

void FSkinningSceneExtension::WaitForHeaderDataUpdateTasks() const
{
	UE::Tasks::Wait(MakeArrayView( { TaskHandles[FreeBufferSpaceTask], TaskHandles[InitHeaderDataTask] } ));
}

FSkinningSceneExtension::FBuffers::FBuffers()
: HeaderDataBuffer(GSkinningBuffersHeaderDataMinSizeBytes >> 2u, TEXT("Skinning.HeaderData"))
, BoneHierarchyBuffer(GSkinningBuffersTransformDataMinSizeBytes >> 2u, TEXT("Skinning.BoneHierarchy"))
, BoneObjectSpaceBuffer(GSkinningBuffersTransformDataMinSizeBytes >> 2u, TEXT("Skinning.BoneObjectSpace"))
, TransformDataBuffer(GSkinningBuffersTransformDataMinSizeBytes >> 2u, TEXT("Skinning.BoneTransforms"))
{
}

FSkinningSceneExtension::FUpdater::FUpdater(FSkinningSceneExtension& InSceneData)
: SceneData(&InSceneData)
, bEnableAsync(GSkinningBuffersAsyncUpdate)
{
}

void FSkinningSceneExtension::FUpdater::End()
{
	// Ensure these tasks finish before we fall out of scope.
	// NOTE: This should be unnecessary if the updater shares the graph builder's lifetime but we don't enforce that
	SceneData->SyncAllTasks();
}

void FSkinningSceneExtension::FUpdater::PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms)
{
	// If there was a pending upload from a prior update (due to the buffer never being used), finish the upload now.
	// This keeps the upload entries from growing unbounded and prevents any undefined behavior caused by any
	// updates that overlap primitives.
	SceneData->FinishSkinningBufferUpload(GraphBuilder);

	if (!SceneData->IsEnabled())
	{
		return;
	}

	SceneData->TaskHandles[FreeBufferSpaceTask] = GraphBuilder.AddSetupTask(
		[this, RemovedList = ChangeSet.RemovedPrimitiveIds]
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::FreeBufferSpace);

			// Remove and free transform data for removed primitives
			// NOTE: Using the ID list instead of the primitive list since we're in an async task
			for (const auto& PersistentIndex : RemovedList)
			{
				if (SceneData->HeaderData.IsValidIndex(PersistentIndex.Index))
				{
					FSkinningSceneExtension::FHeaderData& Data = SceneData->HeaderData[PersistentIndex.Index];

					if (!Data.bIsBatched)
					{
						if (Data.ObjectSpaceBufferOffset != INDEX_NONE)
						{
							SceneData->ObjectSpaceAllocator.Free(Data.ObjectSpaceBufferOffset, Data.ObjectSpaceBufferCount);
						}

						if (Data.HierarchyBufferOffset != INDEX_NONE)
						{
							SceneData->HierarchyAllocator.Free(Data.HierarchyBufferOffset, Data.HierarchyBufferCount);
						}

						if (Data.TransformBufferOffset != INDEX_NONE)
						{
							SceneData->TransformAllocator.Free(Data.TransformBufferOffset, Data.TransformBufferCount);
						}
					}

					SceneData->HeaderData.RemoveAt(PersistentIndex.Index);
					if (!Data.bIsBatched)
					{
						SceneData->HeaderDataIndices.Remove(PersistentIndex.Index);
					}
				}
			}

			// Check to force a full upload by CVar
			// NOTE: Doesn't currently discern which scene to affect
			bForceFullUpload = GSkinningBuffersForceFullUpload != 0;
			if (GSkinningBuffersForceFullUpload == 1)
			{
				GSkinningBuffersForceFullUpload = 0;
			}

			bDefragging = SceneData->ProcessBufferDefragmentation();
			bForceFullUpload |= bDefragging;
		},
		UE::Tasks::ETaskPriority::Normal,
		bEnableAsync
	);
}

void FSkinningSceneExtension::FUpdater::PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet)
{
	if (!SceneData->IsEnabled())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FSkinningSceneExtension::FUpdater::PostSceneUpdate);

	// Cache the updated PrimitiveSceneInfos (this is safe as long as we only access it in updater funcs and RDG setup tasks)
	AddedList = ChangeSet.AddedPrimitiveSceneInfos;

	// Kick off a task to initialize added transform ranges
	if (AddedList.Num() > 0)
	{
		SceneData->TaskHandles[InitHeaderDataTask] = GraphBuilder.AddSetupTask(
			[this]
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::InitHeaderData);

				for (auto PrimitiveSceneInfo : AddedList)
				{
					if (!PrimitiveSceneInfo->Proxy->IsSkinnedMesh())
					{
						continue;
					}

					FSkinningSceneExtensionProxy* Proxy = PrimitiveSceneInfo->Proxy->GetSkinningSceneExtensionProxy();
					if (!Proxy)
					{
						continue;
					}

					const int32 PersistentIndex = PrimitiveSceneInfo->GetPersistentIndex().Index;

					FHeaderData NewHeader;
					NewHeader.InstanceSceneDataOffset     = PrimitiveSceneInfo->GetInstanceSceneDataOffset();
					NewHeader.NumInstanceSceneDataEntries = PrimitiveSceneInfo->GetNumInstanceSceneDataEntries();
					NewHeader.ProviderId                  = Proxy->GetTransformProviderId();
					NewHeader.PrimitiveSceneInfo          = PrimitiveSceneInfo;
					NewHeader.Proxy                       = Proxy;
					NewHeader.MaxTransformCount           = Proxy->GetMaxBoneTransformCount();
					NewHeader.MaxHierarchyCount           = Proxy->GetMaxBoneHierarchyCount();
					NewHeader.MaxObjectSpaceCount         = Proxy->GetMaxBoneObjectSpaceCount();
					NewHeader.MaxInfluenceCount           = Proxy->GetMaxBoneInfluenceCount();
					NewHeader.UniqueAnimationCount        = Proxy->GetUniqueAnimationCount();
					NewHeader.bHasScale                   = Proxy->HasScale();

					SceneData->HeaderData.EmplaceAt(PersistentIndex, NewHeader);

					if (!bForceFullUpload)
					{
						DirtyPrimitiveList.Add(PersistentIndex);
					}
				}
			},
			SceneData->TaskHandles[FreeBufferSpaceTask],
			UE::Tasks::ETaskPriority::Normal,
			bEnableAsync
		);
	}
}

static bool IsValidSkinnedSceneInfo(const FPrimitiveSceneInfo* SceneInfo)
{
	if (SceneInfo == nullptr || SceneInfo->Proxy == nullptr)
	{
		return false;
	}

	if (!SceneInfo->Proxy->GetSkinningSceneExtensionProxy())
	{
		return false;
	}

	return true;
}

void FSkinningSceneExtension::FUpdater::PostMeshUpdate(FRDGBuilder& GraphBuilder, const TConstArrayView<FPrimitiveSceneInfo*>& UpdatedSceneInfoList)
{
	UpdateList = UpdatedSceneInfoList;

	if (SceneData->IsEnabled())
	{
		// Gets the information needed from the primitive for skinning and allocates the appropriate space in the buffer
		// for the primitive's bone transforms
		auto AllocSpaceForPrimitive = [this](const int32 HeaderDataIndex)
		{
			FHeaderData& Data = SceneData->HeaderData[HeaderDataIndex];

			Data.MaxTransformCount		= Data.Proxy->GetMaxBoneTransformCount();
			Data.MaxHierarchyCount		= Data.Proxy->GetMaxBoneHierarchyCount();
			Data.MaxObjectSpaceCount	= Data.Proxy->GetMaxBoneObjectSpaceCount();
			Data.MaxInfluenceCount		= Data.Proxy->GetMaxBoneInfluenceCount();
			Data.UniqueAnimationCount	= Data.Proxy->GetUniqueAnimationCount();

			if (Data.Proxy->UseSkeletonBatching())
			{
				const FSkeletonBatchKey BatchKey
				{
				#if ENABLE_SKELETON_DEBUG_NAME
					.SkeletonName = Data.Proxy->GetSkinnedAsset()->GetSkeleton()->GetFName(),
				#endif
					.SkeletonGuid = Data.Proxy->GetSkinnedAsset()->GetSkeleton()->GetGuid(),
					.TransformProviderId = Data.Proxy->GetTransformProviderId()
				};

				if (SceneData->BatchHeaderData.Contains(BatchKey))
				{
					const FHeaderData& SrcHeaderData = SceneData->BatchHeaderData[BatchKey];

					Data.ObjectSpaceBufferOffset	= SrcHeaderData.ObjectSpaceBufferOffset;
					Data.ObjectSpaceBufferCount		= SrcHeaderData.ObjectSpaceBufferCount;
					Data.HierarchyBufferOffset		= SrcHeaderData.HierarchyBufferOffset;
					Data.HierarchyBufferCount		= SrcHeaderData.HierarchyBufferCount;
					Data.TransformBufferOffset		= SrcHeaderData.TransformBufferOffset;
					Data.TransformBufferCount		= SrcHeaderData.TransformBufferCount;
					Data.bIsBatched					= true;

				#if DO_CHECK
					{
						const FString SkinnedAssetName = Data.Proxy->GetSkinnedAsset()->GetName();
						const FString SkeletonName = Data.Proxy->GetSkinnedAsset()->GetSkeleton()->GetName();

						const uint32 ObjectSpaceFloatCount = Data.Proxy->GetObjectSpaceFloatCount();

						checkf(Data.ObjectSpaceBufferCount == (Data.MaxObjectSpaceCount * ObjectSpaceFloatCount),
							TEXT("Mismatch between ObjectSpaceBufferCount=%d and (MaxObjectSpaceCount * ObjectSpaceFloatCount)=%d for mesh %s with skeleton %s."),
							Data.ObjectSpaceBufferCount, (Data.MaxObjectSpaceCount * ObjectSpaceFloatCount),
							*SkinnedAssetName, *SkeletonName);

						checkf(Data.HierarchyBufferCount == Data.MaxHierarchyCount,
							TEXT("Mismatch between HierarchyBufferCount=%d and MaxHierarchyCount=%d for mesh %s with skeleton %s."),
							Data.HierarchyBufferCount, Data.MaxHierarchyCount,
							*SkinnedAssetName, *SkeletonName);

						checkf(Data.TransformBufferCount == (Data.UniqueAnimationCount * Data.MaxTransformCount * 2u),
							TEXT("Mismatch between TransformBufferCount=%d and (Data.UniqueAnimationCount * Data.MaxTransformCount * 2u)=%d for mesh %s with skeleton %s."),
							Data.TransformBufferCount, (Data.UniqueAnimationCount * Data.MaxTransformCount * 2u),
							*SkinnedAssetName, *SkeletonName);
					}
				#endif

					return;
				}
			}

			bool bRequireUpload = false;

			const uint32 ObjectSpaceNeededSize = Data.MaxObjectSpaceCount * Data.Proxy->GetObjectSpaceFloatCount();
			if (ObjectSpaceNeededSize != Data.ObjectSpaceBufferCount)
			{
				if (Data.ObjectSpaceBufferCount > 0)
				{
					SceneData->ObjectSpaceAllocator.Free(Data.ObjectSpaceBufferOffset, Data.ObjectSpaceBufferCount);
				}

				Data.ObjectSpaceBufferOffset = ObjectSpaceNeededSize > 0 ? SceneData->ObjectSpaceAllocator.Allocate(ObjectSpaceNeededSize) : INDEX_NONE;
				Data.ObjectSpaceBufferCount = ObjectSpaceNeededSize;

				if (!bForceFullUpload)
				{
					bRequireUpload = true;
				}
			}

			const uint32 HierarchyNeededSize = Data.MaxHierarchyCount;
			if (HierarchyNeededSize != Data.HierarchyBufferCount)
			{
				if (Data.HierarchyBufferCount > 0)
				{
					SceneData->HierarchyAllocator.Free(Data.HierarchyBufferOffset, Data.HierarchyBufferCount);
				}

				Data.HierarchyBufferOffset = HierarchyNeededSize > 0 ? SceneData->HierarchyAllocator.Allocate(HierarchyNeededSize) : INDEX_NONE;
				Data.HierarchyBufferCount = HierarchyNeededSize;

				if (!bForceFullUpload)
				{
					bRequireUpload = true;
				}
			}

			const uint32 TransformNeededSize = Data.UniqueAnimationCount * Data.MaxTransformCount * 2u; // Current and Previous
			if (bRequireUpload || (TransformNeededSize != Data.TransformBufferCount))
			{
				if (Data.TransformBufferCount > 0)
				{
					SceneData->TransformAllocator.Free(Data.TransformBufferOffset, Data.TransformBufferCount);
				}

				Data.TransformBufferOffset = TransformNeededSize > 0 ? SceneData->TransformAllocator.Allocate(TransformNeededSize) : INDEX_NONE;
				Data.TransformBufferCount = TransformNeededSize;

				if (!bForceFullUpload)
				{
					bRequireUpload = true;
				}
			}

			if (bRequireUpload)
			{
				DirtyPrimitiveList.Add(Data.PrimitiveSceneInfo->GetPersistentIndex().Index);
			}

			if (Data.Proxy->UseSkeletonBatching())
			{
				Data.bIsBatched = true;

				const FSkeletonBatchKey BatchKey
				{
				#if ENABLE_SKELETON_DEBUG_NAME
					.SkeletonName = Data.Proxy->GetSkinnedAsset()->GetSkeleton()->GetFName(),
				#endif
					.SkeletonGuid = Data.Proxy->GetSkinnedAsset()->GetSkeleton()->GetGuid(),
					.TransformProviderId = Data.Proxy->GetTransformProviderId()
				};

				SceneData->BatchHeaderData.Add(BatchKey, Data);
			}
			else
			{
				SceneData->HeaderDataIndices.Add(HeaderDataIndex);
			}
		};

		// Kick off the allocate task (synced just prior to header uploads)
		SceneData->TaskHandles[AllocBufferSpaceTask] = GraphBuilder.AddSetupTask(
			[this, AllocSpaceForPrimitive]
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::AllocBufferSpace);

				if (bDefragging)
				{
					for (auto& Data : SceneData->HeaderData)
					{
						const int32 HeaderDataIndex = Data.PrimitiveSceneInfo->GetPersistentIndex().Index;
						if (!SceneData->HeaderData.IsValidIndex(HeaderDataIndex))
						{
							// Primitive in update list is either non-Nanite and/or not skinned
							continue;
						}

						AllocSpaceForPrimitive(HeaderDataIndex);
					}
				}
				else
				{
					// Only check to reallocate space for primitives that have requested an update
					for (auto PrimitiveSceneInfo : UpdateList)
					{
						const int32 Index = PrimitiveSceneInfo->GetPersistentIndex().Index;
						if (!SceneData->HeaderData.IsValidIndex(Index))
						{
							// Primitive in update list is either non-Nanite and/or not skinned
							continue;
						}

						AllocSpaceForPrimitive(Index);
					}
				}

				// Only create a new uploader here if one of the two dependent upload tasks will use it
				if (bForceFullUpload || DirtyPrimitiveList.Num() > 0 || UpdateList.Num() > 0)
				{
					SceneData->Uploader = MakeUnique<FUploader>();
				}
			},
			MakeArrayView(
				{
					SceneData->TaskHandles[FreeBufferSpaceTask],
					SceneData->TaskHandles[InitHeaderDataTask]
				}
			),
			UE::Tasks::ETaskPriority::Normal,
			bEnableAsync
		);

		auto UploadHeaderData = [this](const FHeaderData& Data)
		{
			const int32 PersistentIndex = Data.PrimitiveSceneInfo->GetPersistentIndex().Index;

			// Catch when/if no transform buffer data is allocated for a primitive we're tracking.
			// This should be indicative of a bug.
			ensure(Data.HierarchyBufferCount != INDEX_NONE && Data.TransformBufferCount != INDEX_NONE);

			check(SceneData->Uploader.IsValid()); // Sanity check
			SceneData->Uploader->HeaderDataUploader.Add(Data.Pack(), PersistentIndex);
		};

		// Kick off the header data upload task (synced when accessing the buffer)
		SceneData->TaskHandles[UploadHeaderDataTask] = GraphBuilder.AddSetupTask(
			[this, UploadHeaderData]
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::UploadHeaderData);

				if (bForceFullUpload)
				{
					for (auto& Data : SceneData->HeaderData)
					{
						UploadHeaderData(Data);
					}
				}
				else
				{
					// Sort the array so we can skip duplicate entries
					DirtyPrimitiveList.Sort();
					int32 LastPersistentIndex = INDEX_NONE;
					for (auto PersistentIndex : DirtyPrimitiveList)
					{
						if (PersistentIndex != LastPersistentIndex &&
							SceneData->HeaderData.IsValidIndex(PersistentIndex))
						{
							UploadHeaderData(SceneData->HeaderData[PersistentIndex]);
						}
						LastPersistentIndex = PersistentIndex;
					}
				}
			},
			MakeArrayView(
				{
					SceneData->TaskHandles[AllocBufferSpaceTask]
				}
			),
			UE::Tasks::ETaskPriority::Normal,
			bEnableAsync
		);

		auto UploadHierarchyData = [this](const FHeaderData& Data)
		{
			// Bone Hierarchy
			if (Data.MaxHierarchyCount > 0)
			{
				TConstArrayView<uint32> BoneHierarchy = Data.Proxy->GetBoneHierarchy();
				check(BoneHierarchy.Num() == Data.MaxHierarchyCount);
				check(SceneData->Uploader.IsValid());

				auto UploadData = SceneData->Uploader->BoneHierarchyUploader.AddMultiple_GetRef(
					Data.HierarchyBufferOffset,
					Data.HierarchyBufferCount
				);

				uint32* DstBoneHierarchyPtr = UploadData.GetData();
				for (int32 BoneIndex = 0; BoneIndex < Data.MaxHierarchyCount; ++BoneIndex)
				{
					DstBoneHierarchyPtr[BoneIndex] = BoneHierarchy[BoneIndex];
				}
			}

			// Bone Object Space
			if (Data.MaxObjectSpaceCount > 0)
			{
				TConstArrayView<float> BoneObjectSpace = Data.Proxy->GetBoneObjectSpace();
				const uint32 FloatCount = Data.Proxy->GetObjectSpaceFloatCount();
				check(BoneObjectSpace.Num() == Data.MaxObjectSpaceCount * FloatCount);

				auto UploadData = SceneData->Uploader->BoneObjectSpaceUploader.AddMultiple_GetRef(
					Data.ObjectSpaceBufferOffset,
					Data.ObjectSpaceBufferCount
				);

				float* DstBoneObjectSpacePtr = UploadData.GetData();
				for (uint32 BoneFloatIndex = 0; BoneFloatIndex < (Data.MaxObjectSpaceCount * FloatCount); ++BoneFloatIndex)
				{
					DstBoneObjectSpacePtr[BoneFloatIndex] = BoneObjectSpace[BoneFloatIndex];
				}
			}
		};

		auto UploadTransformData = [this](const FHeaderData& Data, bool bProvidersEnabled)
		{
			if (bProvidersEnabled && Data.Proxy->GetTransformProviderId().IsValid())
			{
				return;
			}

			// NOTE: This path is purely for debugging now - should also set "r.Skinning.Buffers.ForceFullUpload 2" to avoid caching artifacts

			check(SceneData->Uploader.IsValid());
			auto UploadData = SceneData->Uploader->TransformDataUploader.AddMultiple_GetRef(
				Data.TransformBufferOffset,
				Data.TransformBufferCount
			);

			check(Data.UniqueAnimationCount * Data.MaxTransformCount * 2u == Data.TransformBufferCount);

			FCompressedBoneTransform* DstCurrentBoneTransformsPtr = UploadData.GetData();
			FCompressedBoneTransform* DstPreviousBoneTransformsPtr = DstCurrentBoneTransformsPtr + Data.MaxTransformCount;
			const uint32 StridedPtrStep = Data.MaxTransformCount * 2u;

			for (int32 UniqueAnimation = 0; UniqueAnimation < Data.UniqueAnimationCount; ++UniqueAnimation)
			{
				for (int32 TransformIndex = 0; TransformIndex < Data.MaxTransformCount; ++TransformIndex)
				{
					SetCompressedBoneTransformIdentity(DstCurrentBoneTransformsPtr[TransformIndex]);
					SetCompressedBoneTransformIdentity(DstPreviousBoneTransformsPtr[TransformIndex]);
				}

				DstCurrentBoneTransformsPtr += StridedPtrStep;
				DstPreviousBoneTransformsPtr += StridedPtrStep;
			}
		};

		// Kick off the hierarchy data upload task (synced when accessing the buffer)
		SceneData->TaskHandles[UploadHierarchyDataTask] = GraphBuilder.AddSetupTask(
			[this, UploadHierarchyData]
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::UploadHierarchyData);

				if (bForceFullUpload)
				{
					for (auto& Data : SceneData->HeaderData)
					{
						UploadHierarchyData(Data);
					}
				}
				else
				{
					for (auto PrimitiveSceneInfo : UpdateList)
					{
						const int32 PersistentIndex = PrimitiveSceneInfo->GetPersistentIndex().Index;
						if (!SceneData->HeaderData.IsValidIndex(PersistentIndex))
						{
							// Primitive in update list is either non-Nanite and/or not skinned
							continue;
						}
						check(IsValidSkinnedSceneInfo(PrimitiveSceneInfo));
						UploadHierarchyData(SceneData->HeaderData[PersistentIndex]);
					}
				}
			},
			MakeArrayView({ SceneData->TaskHandles[AllocBufferSpaceTask] }),
			UE::Tasks::ETaskPriority::Normal,
			bEnableAsync
		);

		// Kick off the transform data upload task (synced when accessing the buffer)
		SceneData->TaskHandles[UploadTransformDataTask] = GraphBuilder.AddSetupTask(
			[this, UploadTransformData]
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Skinning::UploadTransformData);

				const bool bProvidersEnabled = GSkinningTransformProviders;

				if (bForceFullUpload)
				{
					for (auto& Data : SceneData->HeaderData)
					{
						UploadTransformData(Data, bProvidersEnabled);
					}
				}
				else
				{
					for (auto PrimitiveSceneInfo : UpdateList)
					{
						const int32 PersistentIndex = PrimitiveSceneInfo->GetPersistentIndex().Index;
						if (!SceneData->HeaderData.IsValidIndex(PersistentIndex))
						{
							// Primitive in update list is either non-Nanite and/or not skinned
							continue;
						}
						check(IsValidSkinnedSceneInfo(PrimitiveSceneInfo));
						UploadTransformData(SceneData->HeaderData[PersistentIndex], bProvidersEnabled);
					}
				}
			},
			MakeArrayView({ SceneData->TaskHandles[AllocBufferSpaceTask] }),
			UE::Tasks::ETaskPriority::Normal,
			bEnableAsync
		);

		if (!bEnableAsync)
		{
			// If disabling async, just finish the upload immediately
			SceneData->FinishSkinningBufferUpload(GraphBuilder);
		}
	}
}

class FNaniteSkinningUpdateViewDataCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteSkinningUpdateViewDataCS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteSkinningUpdateViewDataCS, FGlobalShader)

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneResourceParameters, GPUScene)
		SHADER_PARAMETER_STRUCT_INCLUDE(RendererViewData::FWriterParameters, ViewDataParametersWriter)
		SHADER_PARAMETER_STRUCT_INCLUDE( FInstanceHierarchyParameters, InstanceHierarchyParameters )
		SHADER_PARAMETER_RDG_BUFFER_SRV( StructuredBuffer< FUintVector2 >, InstanceWorkGroups )
		SHADER_PARAMETER(float, DefaultAnimationMinScreenSize)		
		RDG_BUFFER_ACCESS( IndirectArgs,	ERHIAccess::IndirectArgs )
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int ThreadGroupSize = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("VIEW_DATA_ACCESS_MODE"), VIEW_DATA_ACCESS_RW);
		// Don't access the global Scene uniform buffer but map to indivdual UBs for each used module.
		OutEnvironment.SetDefine(TEXT("USE_EXPLICIT_SCENE_UB_MODULES"), 1);

		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};
IMPLEMENT_GLOBAL_SHADER(FNaniteSkinningUpdateViewDataCS, "/Engine/Private/Nanite/NaniteSkinningUpdateViewData.usf", "NaniteSkinningUpdateViewDataCS", SF_Compute);

class FNaniteSkinningUpdateChunkCullCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteSkinningUpdateChunkCullCS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteSkinningUpdateChunkCullCS, FGlobalShader)

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(RendererViewData::FWriterParameters, ViewDataParametersWriter)
		SHADER_PARAMETER_STRUCT_INCLUDE( FInstanceHierarchyParameters, InstanceHierarchyParameters )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWStructuredBuffer< FUintVector2 >, OutInstanceWorkGroups )
		SHADER_PARAMETER_RDG_BUFFER_UAV( RWBuffer< uint >, OutInstanceWorkArgs )

		SHADER_PARAMETER(float, DefaultAnimationMinScreenSize)		
	END_SHADER_PARAMETER_STRUCT()

	static constexpr int ThreadGroupSize = 64;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("VIEW_DATA_ACCESS_MODE"), VIEW_DATA_ACCESS_RW);
		// Don't access the global Scene uniform buffer but map to indivdual UBs for each used module.
		OutEnvironment.SetDefine(TEXT("USE_EXPLICIT_SCENE_UB_MODULES"), 1);

		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
	}
};
IMPLEMENT_GLOBAL_SHADER(FNaniteSkinningUpdateChunkCullCS, "/Engine/Private/Nanite/NaniteSkinningUpdateViewData.usf", "NaniteSkinningUpdateChunkCullCS", SF_Compute);


void FSkinningSceneExtension::FRenderer::UpdateViewData(FRDGBuilder& GraphBuilder, const FRendererViewDataManager& ViewDataManager)
{
	SCOPED_NAMED_EVENT(FSkinningSceneExtension_FRenderer_UpdateViewData, FColor::Silver);

	FSceneCullingRenderer* SceneCullingRenderer = GetSceneRenderer().GetSceneExtensionsRenderers().GetRendererPtr<FSceneCullingRenderer>();
	if (!SceneCullingRenderer || !SceneCullingRenderer->IsEnabled())
	{
		return;
	}

	FInstanceHierarchyParameters InstanceHierarchyParameters = SceneCullingRenderer->GetShaderParameters(GraphBuilder);
	int32 NumAllocatedChunks = InstanceHierarchyParameters.NumAllocatedChunks;
	// Create a buffer with enough space for all chunks
	FRDGBufferRef InstanceWorkGroupsRDG = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FUintVector2), NumAllocatedChunks), TEXT("Skinning.UpdateViewData.WorkGroups"));
	ERHIFeatureLevel::Type FeatureLevel = SceneData->Scene.GetFeatureLevel();
	FRDGBufferRef InstanceWorkArgsRDG = CreateAndClearIndirectDispatchArgs1D(GraphBuilder, FeatureLevel, TEXT("Skinning.UpdateViewData.IndirectArgs"));
	{
		FNaniteSkinningUpdateChunkCullCS::FParameters* PassParameters = GraphBuilder.AllocParameters< FNaniteSkinningUpdateChunkCullCS::FParameters >();
		PassParameters->InstanceHierarchyParameters = InstanceHierarchyParameters;
		PassParameters->DefaultAnimationMinScreenSize = GSkinningDefaultAnimationMinScreenSize;

		PassParameters->OutInstanceWorkGroups = GraphBuilder.CreateUAV(InstanceWorkGroupsRDG);
		PassParameters->OutInstanceWorkArgs = GraphBuilder.CreateUAV(InstanceWorkArgsRDG);
		PassParameters->ViewDataParametersWriter = ViewDataManager.GetWriterShaderParameters(GraphBuilder);

		auto ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FNaniteSkinningUpdateChunkCullCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "NaniteSkinningUpdateViewDataChunks" ),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(NumAllocatedChunks, 64)
		);
	}

	{
		FNaniteSkinningUpdateViewDataCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteSkinningUpdateViewDataCS::FParameters>();
		PassParameters->GPUScene = SceneData->Scene.GPUScene.GetShaderParameters(GraphBuilder);
		PassParameters->ViewDataParametersWriter = ViewDataManager.GetWriterShaderParameters(GraphBuilder);
		PassParameters->InstanceHierarchyParameters = InstanceHierarchyParameters;
		PassParameters->DefaultAnimationMinScreenSize = GSkinningDefaultAnimationMinScreenSize;
		PassParameters->IndirectArgs = InstanceWorkArgsRDG;
		PassParameters->InstanceWorkGroups = GraphBuilder.CreateSRV(InstanceWorkGroupsRDG);

		auto ComputeShader = GetGlobalShaderMap(SceneData->Scene.GetFeatureLevel())->GetShader<FNaniteSkinningUpdateViewDataCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME( "NaniteSkinningUpdateViewData" ),
			ComputeShader,
			PassParameters,
			PassParameters->IndirectArgs,
			0
		);
	}
}

void FSkinningSceneExtension::FRenderer::UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniformBuffer)
{
	SCOPED_NAMED_EVENT(FSkinningSceneExtension_FRenderer_UpdateSceneUniformBuffer, FColor::Silver);
	check(SceneData->IsEnabled());
	FSkinningSceneParameters Parameters;
	SceneData->FinishSkinningBufferUpload(GraphBuilder, &Parameters);
	SceneUniformBuffer.Set(SceneUB::Skinning, Parameters);
	SceneData->PerformSkinning(Parameters, GraphBuilder);
}

void FSkinningSceneExtension::GetSkinnedPrimitives(TArray<FPrimitiveSceneInfo*>& OutPrimitives) const
{
	OutPrimitives.Reset();

	if (!IsEnabled())
	{
		return;
	}

	WaitForHeaderDataUpdateTasks();

	OutPrimitives.Reserve(HeaderData.Num());

	for (typename TSparseArray<FHeaderData>::TConstIterator It(HeaderData); It; ++It)
	{
		const FHeaderData& Header = *It;
		OutPrimitives.Add(Header.PrimitiveSceneInfo);
	}
}

const FSkinningTransformProvider::FProviderId& FSkinningSceneExtension::GetRefPoseProviderId()
{
	return RefPoseProviderId;
}

const FSkinningTransformProvider::FProviderId& FSkinningSceneExtension::GetAnimRuntimeProviderId()
{
	return AnimRuntimeProviderId;
}

void FSkinningSceneExtension::ProvideRefPoseTransforms(FSkinningTransformProvider::FProviderContext& Context)
{
	const uint32 TransformsPerGroup = FRefPoseTransformProviderCS::TransformsPerGroup;

	// TODO: Optimize further

	uint32 BlockCount = 0;
	for (const FSkinningTransformProvider::FProviderIndirection Indirection : Context.Indirections)
	{
		const FSkinningSceneExtensionProxy* Proxy = Context.Proxies[Indirection.Index];
		const uint32 TransformCount = Proxy->GetMaxBoneTransformCount();
		const uint32 AnimationCount = Proxy->GetUniqueAnimationCount();
		BlockCount += FMath::DivideAndRoundUp(TransformCount * AnimationCount, TransformsPerGroup);
	}

	if (BlockCount == 0)
	{
		return;
	}

	FRDGBuilder& GraphBuilder = Context.GraphBuilder;
	FTransformBlockHeader* BlockHeaders = GraphBuilder.AllocPODArray<FTransformBlockHeader>(BlockCount);

	uint32 BlockWrite = 0;
	for (const FSkinningTransformProvider::FProviderIndirection Indirection : Context.Indirections)
	{
		const FPrimitiveSceneInfo* Primitive = Context.Primitives[Indirection.Index];
		const FSkinningSceneExtensionProxy* Proxy = Context.Proxies[Indirection.Index];
		const uint32 TransformCount = Proxy->GetMaxBoneTransformCount();
		const uint32 AnimationCount = Proxy->GetUniqueAnimationCount();
		const uint32 TotalTransformCount = TransformCount * AnimationCount;

		uint32 TransformWrite = Indirection.TransformOffset;

		const uint32 FullBlockCount = TotalTransformCount / TransformsPerGroup;
		for (uint32 BlockIndex = 0; BlockIndex < FullBlockCount; ++BlockIndex)
		{
			BlockHeaders[BlockWrite].BlockLocalIndex = BlockIndex;
			BlockHeaders[BlockWrite].BlockTransformCount = TransformsPerGroup;
			BlockHeaders[BlockWrite].BlockTransformOffset = TransformWrite;
			++BlockWrite;

			TransformWrite += (TransformsPerGroup * 2 * sizeof(FCompressedBoneTransform));
		}

		const uint32 PartialTransformCount = TotalTransformCount - (FullBlockCount * TransformsPerGroup);
		if (PartialTransformCount > 0)
		{
			BlockHeaders[BlockWrite].BlockLocalIndex = FullBlockCount;
			BlockHeaders[BlockWrite].BlockTransformCount = PartialTransformCount;
			BlockHeaders[BlockWrite].BlockTransformOffset = TransformWrite;
			++BlockWrite;
		}
	}

	FRDGBufferRef BlockHeaderBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("Skinning.RefPoseHeaders"),
		sizeof(FTransformBlockHeader),
		FMath::RoundUpToPowerOfTwo(FMath::Max(BlockCount, 1u)),
		BlockHeaders,
		sizeof(FTransformBlockHeader) * BlockCount,
		// The buffer data is allocated above on the RDG timeline
		ERDGInitialDataFlags::NoCopy
	);

	FRefPoseTransformProviderCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRefPoseTransformProviderCS::FParameters>();
	PassParameters->TransformBuffer = GraphBuilder.CreateUAV(Context.TransformBuffer);
	PassParameters->HeaderBuffer = GraphBuilder.CreateSRV(BlockHeaderBuffer);

	auto ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FRefPoseTransformProviderCS>();
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("RefPoseProvider"),
		ComputeShader,
		PassParameters,
		FIntVector(BlockCount, 1, 1)
	);
}

BEGIN_SHADER_PARAMETER_STRUCT(FCopyBufferParameters, )
	RDG_BUFFER_ACCESS(SrcBuffer, ERHIAccess::CopySrc)
	RDG_BUFFER_ACCESS(DstBuffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

void FSkinningSceneExtension::ProvideAnimRuntimeTransforms(FSkinningTransformProvider::FProviderContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSkinningSceneExtension::ProvideAnimRuntimeTransforms);
	RDG_EVENT_SCOPE(Context.GraphBuilder, "ProvideAnimRuntimeTransforms");

	uint32 GlobalTransformCount = 0;

	for (const FSkinningTransformProvider::FProviderIndirection Indirection : Context.Indirections)
	{
		const FSkinningSceneExtensionProxy* Proxy = Context.Proxies[Indirection.Index];
		const uint32 TransformCount = Proxy->GetMaxBoneTransformCount();
		const uint32 AnimationCount = Proxy->GetUniqueAnimationCount();
		GlobalTransformCount += (TransformCount * AnimationCount) * 2; // Current and Previous
	}

	if (GlobalTransformCount == 0)
	{
		return;
	}

	FRDGBuilder& GraphBuilder = Context.GraphBuilder;
	FRDGAsyncScatterUploadBuffer TransformUploadBuffer;
	FRDGScatterUploadBuilder* Builder = FRDGScatterUploadBuilder::Create(GraphBuilder);

	Builder->AddPass(GraphBuilder, TransformUploadBuffer, Context.TransformBuffer, GlobalTransformCount, sizeof(FCompressedBoneTransform), TEXT("Skinning.AnimTransforms"),
		[Indirections = Context.Indirections, Proxies = Context.Proxies] (FRDGScatterUploader& ScatterUploader)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSkinningSceneExtension::ProvideAnimRuntimeTransformsTask);

		for (const FSkinningTransformProvider::FProviderIndirection Indirection : Indirections)
		{
			const FSkinningSceneExtensionProxy* Proxy = Proxies[Indirection.Index];

			const uint32 MaxTransformCount = Proxy->GetMaxBoneTransformCount();
			const uint32 MaxTotalTransformCount = MaxTransformCount * 2u; // Current and Previous

			const FSkeletalMeshObject* MeshObject = Proxy->GetMeshObject();
			TConstArrayView<FMatrix44f> SrcCurrentTransforms  = MeshObject->GetReferenceToLocalMatrices();
			TConstArrayView<FMatrix44f> SrcPreviousTransforms = MeshObject->GetPrevReferenceToLocalMatrices();

			const int32 DstTransformIndex = Indirection.TransformOffset / sizeof(FCompressedBoneTransform);

			if (!SrcCurrentTransforms.IsEmpty())
			{
				if (Proxy->UseSectionBoneMap())
				{
					const int32 LODIndex = MeshObject->GetLOD();
					TConstArrayView<FSkelMeshRenderSection> Sections = MeshObject->GetRenderSections(LODIndex);

					int32 NumBones = 0;
					for (const FSkelMeshRenderSection& Section : Sections)
					{
						if (Section.IsValid())
						{
							NumBones += Section.BoneMap.Num();
						}
					}

					{
						TArrayView<FCompressedBoneTransform> DstCurrentTransforms = ScatterUploader.Add_GetRef<FCompressedBoneTransform>(DstTransformIndex, NumBones);
						int32 TransformIndex = 0;

						for (const FSkelMeshRenderSection& Section : Sections)
						{
							if (Section.IsValid())
							{
								for (FBoneIndexType BoneIndex : Section.BoneMap)
								{
									StoreCompressedBoneTransform(&DstCurrentTransforms[TransformIndex++], SrcCurrentTransforms[BoneIndex]);
								}
							}
						}
					}

					if (!SrcPreviousTransforms.IsEmpty())
					{
						TArrayView<FCompressedBoneTransform> DstCurrentTransforms = ScatterUploader.Add_GetRef<FCompressedBoneTransform>(DstTransformIndex + MaxTransformCount, NumBones);
						int32 TransformIndex = 0;

						for (const FSkelMeshRenderSection& Section : Sections)
						{
							if (Section.IsValid())
							{
								for (FBoneIndexType BoneIndex : Section.BoneMap)
								{
									StoreCompressedBoneTransform(&DstCurrentTransforms[TransformIndex++], SrcPreviousTransforms[BoneIndex]);
								}
							}
						}
					}
				}
				else
				{
					{
						TArrayView<FCompressedBoneTransform> DstCurrentTransforms = ScatterUploader.Add_GetRef<FCompressedBoneTransform>(DstTransformIndex, MaxTransformCount);

						for (uint32 TransformIndex = 0; TransformIndex < MaxTransformCount; ++TransformIndex)
						{
							StoreCompressedBoneTransform(&DstCurrentTransforms[TransformIndex], SrcCurrentTransforms[TransformIndex]);
						}
					}

					if (!SrcPreviousTransforms.IsEmpty())
					{
						TArrayView<FCompressedBoneTransform> DstPreviousTransforms = ScatterUploader.Add_GetRef<FCompressedBoneTransform>(DstTransformIndex + MaxTransformCount, MaxTransformCount);

						for (uint32 TransformIndex = 0; TransformIndex < MaxTransformCount; ++TransformIndex)
						{
							StoreCompressedBoneTransform(&DstPreviousTransforms[TransformIndex], SrcPreviousTransforms[TransformIndex]);
						}
					}
				}
			}
			else
			{
				FCompressedBoneTransform* DstTransforms = (FCompressedBoneTransform*)ScatterUploader.Add_GetRef(DstTransformIndex, MaxTotalTransformCount);

				// Data is invalid, replace with reference pose
				for (uint32 TransformIndex = 0; TransformIndex < MaxTotalTransformCount; ++TransformIndex)
				{
					SetCompressedBoneTransformIdentity(DstTransforms[TransformIndex]);
				}
			}
		}
	});

	Builder->Execute(GraphBuilder);
}

// TODO: these are prototype macros for how we might expose SceneUB for direct binding. 
//       If this becomes the way we want to expose this, then we should move this to shared headers.
//       There's still some machinery we _could_ add to make it work nicely as an API, e.g., interface to get the associated sub-UB & register a provider (or something).

#define IMPLEMENT_STATIC_UNIFORM_BUFFER_SCENE_UB(StructType, MangledName) \
	IMPLEMENT_STATIC_UNIFORM_BUFFER_SLOT(MangledName)	\
	IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(StructType, #MangledName, MangledName);

/**
 * Implement a Scene UB sub-struct _with_ a global UB definition for binding stand-alone.
 */
#define IMPLEMENT_SCENE_UB_STRUCT_EX(StructType, FieldName, DefaultValueFactoryType) \
	TSceneUniformBufferMemberRegistration<StructType> SceneUB::FieldName { TEXT(#FieldName), DefaultValueFactoryType }; \
	IMPLEMENT_STATIC_UNIFORM_BUFFER_SCENE_UB(StructType, SceneUbEx##FieldName)

IMPLEMENT_SCENE_UB_STRUCT_EX(FSkinningSceneParameters, Skinning, GetDefaultSkinningParameters);
