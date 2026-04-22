// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	MetalRayTracing.cpp: MetalRT Implementation
==============================================================================*/

#include "MetalRayTracing.h"

#if METAL_RHI_RAYTRACING

#include "MetalDynamicRHI.h"
#include "MetalRHIContext.h"
#include "MetalShaderTypes.h"
#include "MetalBindlessDescriptors.h"
#include "BuiltInRayTracingShaders.h"
#include "RayTracingValidationShaders.h"
#include "Async/ParallelFor.h"

static int32 GMetalRayTracingAllowCompaction = 1;
static FAutoConsoleVariableRef CVarMetalRayTracingAllowCompaction(
	TEXT("r.Metal.RayTracing.AllowCompaction"),
	GMetalRayTracingAllowCompaction,
	TEXT("Whether to automatically perform compaction for static acceleration structures to save GPU memory. (default = 1)\n"),
	ECVF_ReadOnly
);

static int32 GRayTracingDebugForceBuildMode = 0;
static FAutoConsoleVariableRef CVarMetalRayTracingDebugForceFastTrace(
	TEXT("r.Metal.RayTracing.DebugForceBuildMode"),
	GRayTracingDebugForceBuildMode,
	TEXT("Forces specific acceleration structure build mode (not runtime-tweakable).\n")
	TEXT("0: Use build mode requested by high-level code (Default)\n")
	TEXT("1: Force fast build mode\n")
	TEXT("2: Force fast trace mode\n"),
	ECVF_ReadOnly
);

static int32 GMetalRayTracingMaxBatchedCompaction = 64;
static FAutoConsoleVariableRef CVarMetalRayTracingMaxBatchedCompaction(
	TEXT("r.Metal.RayTracing.MaxBatchedCompaction"),
	GMetalRayTracingMaxBatchedCompaction,
	TEXT("Maximum of amount of compaction requests and rebuilds per frame. (default = 64)\n"),
	ECVF_ReadOnly
);

FMetalAccelerationStructure::~FMetalAccelerationStructure()
{
	if(IndirectArgumentBuffer)
	{
		FMetalDynamicRHI::Get().DeferredDelete(IndirectArgumentBuffer);
	}
	AccelerationStructure.reset();
}

void FMetalAccelerationStructure::SetIndirectArgumentBuffer(FMetalBufferPtr IndirectArgs)
{
	if(IndirectArgumentBuffer)
	{
		FMetalDynamicRHI::Get().DeferredDelete(IndirectArgumentBuffer);
	}
	IndirectArgumentBuffer = IndirectArgs;
}

static ERayTracingAccelerationStructureFlags GetRayTracingAccelerationStructureBuildFlags(const FRayTracingGeometryInitializer& Initializer)
{
	ERayTracingAccelerationStructureFlags BuildFlags = ERayTracingAccelerationStructureFlags::None;

	if (Initializer.bFastBuild)
	{
		BuildFlags = ERayTracingAccelerationStructureFlags::FastBuild;
	}
	else
	{
		BuildFlags = ERayTracingAccelerationStructureFlags::FastTrace;
	}

	if (Initializer.bAllowUpdate)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate);
	}

	if (!Initializer.bFastBuild && !Initializer.bAllowUpdate && Initializer.bAllowCompaction && GMetalRayTracingAllowCompaction)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction);
	}

	if (GRayTracingDebugForceBuildMode == 1)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::FastBuild);
		EnumRemoveFlags(BuildFlags, ERayTracingAccelerationStructureFlags::FastTrace);
	}
	else if (GRayTracingDebugForceBuildMode == 2)
	{
		EnumAddFlags(BuildFlags, ERayTracingAccelerationStructureFlags::FastTrace);
		EnumRemoveFlags(BuildFlags, ERayTracingAccelerationStructureFlags::FastBuild);
	}

	return BuildFlags;
}

static bool ShouldCompactAfterBuild(ERayTracingAccelerationStructureFlags BuildFlags)
{
	return EnumHasAllFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction | ERayTracingAccelerationStructureFlags::FastTrace)
		&& !EnumHasAnyFlags(BuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate);
}

// Manages all the pending BLAS compaction requests
class FMetalRayTracingCompactionRequestHandler
{
public:
	UE_NONCOPYABLE(FMetalRayTracingCompactionRequestHandler)

	FMetalRayTracingCompactionRequestHandler(FMetalDevice& DeviceContext);
	~FMetalRayTracingCompactionRequestHandler();

	void RequestCompact(FMetalRayTracingGeometry* InRTGeometry);
	bool ReleaseRequest(FMetalRayTracingGeometry* InRTGeometry);

	void Update(FMetalRHICommandContext& Context);

private:
	/** Enqueued requests (waiting on size request submit). */
	TArray<FMetalRayTracingGeometry*> PendingRequests;

	/** Enqueued compaction requests (submitted compaction size requests; waiting on readback and actual compaction). */
	TArray<FMetalRayTracingGeometry*> ActiveRequests;

	/** Buffer used for compacted size readback. */
	FMetalBufferPtr CompactedStructureSizeBuffer;

	/** Size entry allocated in the CompactedStructureSizeBuffer (in element count). */
	uint32 SizeBufferMaxCapacity;
	
	FCriticalSection CS;
	FMetalSyncPoint* CompactionQuerySyncPoint = nullptr;
};

FMetalRayTracingCompactionRequestHandler::FMetalRayTracingCompactionRequestHandler(FMetalDevice& Device)
	: SizeBufferMaxCapacity(GMetalRayTracingMaxBatchedCompaction)
{
	MTL::Buffer* BufferPtr = Device.GetDevice()->newBuffer(GMetalRayTracingMaxBatchedCompaction * sizeof(uint32), MTL::ResourceStorageModeShared);
	CompactedStructureSizeBuffer = FMetalBufferPtr(new FMetalBuffer(BufferPtr, FMetalBuffer::FreePolicy::Owner));

	check(CompactedStructureSizeBuffer);
}

FMetalRayTracingCompactionRequestHandler::~FMetalRayTracingCompactionRequestHandler()
{
	check(PendingRequests.IsEmpty());
	
	FMetalDynamicRHI::Get().DeferredDelete(CompactedStructureSizeBuffer);
	
	if(CompactionQuerySyncPoint)
	{
		CompactionQuerySyncPoint->Release();
		CompactionQuerySyncPoint = nullptr;
	}
}

void FMetalRayTracingCompactionRequestHandler::RequestCompact(FMetalRayTracingGeometry* InRTGeometry)
{
	FScopeLock Lock(&CS);
	
	check(InRTGeometry->GetAccelerationStructure());
	ERayTracingAccelerationStructureFlags GeometryBuildFlags = GetRayTracingAccelerationStructureBuildFlags(InRTGeometry->Initializer);
	check(EnumHasAllFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::AllowCompaction) &&
		EnumHasAllFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::FastTrace) &&
		!EnumHasAnyFlags(GeometryBuildFlags, ERayTracingAccelerationStructureFlags::AllowUpdate));

	PendingRequests.Add(InRTGeometry);
}

bool FMetalRayTracingCompactionRequestHandler::ReleaseRequest(FMetalRayTracingGeometry* InRTGeometry)
{
	FScopeLock Lock(&CS);

	// Remove from pending list, not found then try active requests
	if (PendingRequests.Remove(InRTGeometry) <= 0)
	{
		// If currently enqueued, then clear pointer to not handle the compaction request anymore			
		for (int32 BLASIndex = 0; BLASIndex < ActiveRequests.Num(); ++BLASIndex)
		{
			if (ActiveRequests[BLASIndex] == InRTGeometry)
			{
				ActiveRequests[BLASIndex] = nullptr;
				return true;
			}
		}

		return false;
	}
	else
	{
		return true;
	}
	
	return true;
}

void FMetalRHICommandContext::WriteCompactedAccelerationStructureSize(MTLAccelerationStructurePtr AccelerationStructure, FMetalBufferPtr CompactedStructureSizeBuffer, uint32 Offset)
{
	if (!CurrentEncoder.GetCommandBuffer())
	{
		StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	if(!CurrentEncoder.IsAccelerationStructureCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		
		// TODO: Carl - Sample Counters
		CurrentEncoder.BeginAccelerationStructureCommandEncoding();
	}

	MTL::AccelerationStructureCommandEncoder* CommandEncoder = CurrentEncoder.GetAccelerationStructureCommandEncoder();
	CommandEncoder->writeCompactedAccelerationStructureSize(AccelerationStructure.get(), CompactedStructureSizeBuffer->GetMTLBuffer(), Offset);
}

void FMetalRHICommandContext::CopyAndCompactAccelerationStructure(MTLAccelerationStructurePtr AccelerationStructureSrc, MTLAccelerationStructurePtr AccelerationStructureDest)
{
	if (!CurrentEncoder.GetCommandBuffer())
	{
		StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	if(!CurrentEncoder.IsAccelerationStructureCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		
		// TODO: Carl - Sample Counters
		CurrentEncoder.BeginAccelerationStructureCommandEncoding();
	}

	MTL::AccelerationStructureCommandEncoder* CommandEncoder = CurrentEncoder.GetAccelerationStructureCommandEncoder();
	CommandEncoder->copyAndCompactAccelerationStructure(AccelerationStructureSrc.get(), AccelerationStructureDest.get());
}

void FMetalRayTracingCompactionRequestHandler::Update(FMetalRHICommandContext& Context)
{
	FScopeLock Lock(&CS);
	
	// Early exit to avoid unecessary encoding breaks.
	if (PendingRequests.IsEmpty() && ActiveRequests.IsEmpty())
	{
		return;
	}
	
	// Process active requests.
	if(CompactionQuerySyncPoint && CompactionQuerySyncPoint->IsComplete())
	{
		// Try to readback active requests.
		uint32* CompactedSizes = (uint32*)CompactedStructureSizeBuffer->Contents();
		
		for (FMetalRayTracingGeometry* ActiveRequestsTail : ActiveRequests)
		{	
			if(!ActiveRequestsTail)
			{
				continue;
			}
			
			uint32 CompactedSize = CompactedSizes[ActiveRequestsTail->CompactionSizeIndex];
			check(CompactedSize != 0);
			
			FMetalAccelerationStructure* SrcBuffer = ActiveRequestsTail->GetAccelerationStructure();
			MTLAccelerationStructurePtr SrcBLAS = SrcBuffer->GetAccelerationStructure();
			
			// Allocate new acceleration structure with compacted size
			FMetalAccelerationStructure* DestAccelerationStructure = Context.GetDevice().GetResourceHeap().CreateAccelerationStructure(CompactedSize);
			
			if(GetEmitDrawEvents())
			{
				FString DebugNameString = ActiveRequestsTail->Initializer.DebugName.ToString();
				DestAccelerationStructure->GetAccelerationStructure()->setLabel(FStringToNSString(DebugNameString));
			}
			check(DestAccelerationStructure);
			
			MTLAccelerationStructurePtr DestBLAS = DestAccelerationStructure->GetAccelerationStructure();
			Context.CopyAndCompactAccelerationStructure(SrcBLAS, DestBLAS);
			
			// Swap acceleration structure buffers 
			ActiveRequestsTail->SetAccelerationStructure(DestAccelerationStructure);	
		}
		
		ActiveRequests.Empty(ActiveRequests.Num());
		
		CompactionQuerySyncPoint->Release();
		CompactionQuerySyncPoint = nullptr;
	}
	
	// Process pending requests.
	if(ActiveRequests.IsEmpty())
	{
		uint32* CompactedSizes = (uint32*)CompactedStructureSizeBuffer->Contents();
		uint32_t CompactionIndex = 0;
		
		while (!PendingRequests.IsEmpty())
		{
			FMetalRayTracingGeometry* Geometry = PendingRequests[0];
			
			Geometry->CompactionSizeIndex = CompactionIndex++;
			CompactedSizes[Geometry->CompactionSizeIndex] = 0;
			
			FMetalAccelerationStructure* AccelerationStructure = Geometry->GetAccelerationStructure();
			Context.WriteCompactedAccelerationStructureSize(AccelerationStructure->GetAccelerationStructure(), CompactedStructureSizeBuffer, Geometry->CompactionSizeIndex * sizeof(uint32));
			
			ActiveRequests.Add(Geometry);			
			PendingRequests.Remove(Geometry);
			
			// enqueued enough requests for this update round
			if (ActiveRequests.Num() >= GMetalRayTracingMaxBatchedCompaction)
			{
				break;
			}
		}
		
		CompactionQuerySyncPoint = Context.GetContextSyncPoint();
		CompactionQuerySyncPoint->AddRef();
	}
}

/** Fills a MTLPrimitiveAccelerationStructureDescriptor with infos provided by the UE5 geometry descriptor.
 * This function assumes that GeometryDescriptors has already been allocated, and that you are responsible of its lifetime.
 */
static void FillPrimitiveAccelerationStructureDesc(FMetalDevice& Device, MTL::PrimitiveAccelerationStructureDescriptor* AccelerationStructureDescriptor, const FRayTracingGeometryInitializer& Initializer)
{
	MTL_SCOPED_AUTORELEASE_POOL;
	
	TArray<MTL::AccelerationStructureTriangleGeometryDescriptor*> GeometryDescriptors;
	
	// Populate Segments Descriptors.
	FMetalRHIBuffer* IndexBuffer = ResourceCast(Initializer.IndexBuffer.GetReference());

	int32 SegmentIndex = 0;
	for (const FRayTracingGeometrySegment& Segment : Initializer.Segments)
	{
		// Vertex Buffer Infos
		FMetalRHIBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
		check(VertexBuffer);

		MTL::AccelerationStructureTriangleGeometryDescriptor* GeometryDescriptor = MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
		
		switch (Segment.VertexBufferElementType)
		{
			case VET_Float4:
				// Rely on vertex buffer element stride to support fallthrough correctly
			case VET_Float3:
				GeometryDescriptor->setVertexFormat(MTL::AttributeFormatFloat3);
				break;
			case VET_Float2:
				GeometryDescriptor->setVertexFormat(MTL::AttributeFormatFloat2);
				break;
			case VET_Half2:
				GeometryDescriptor->setVertexFormat(MTL::AttributeFormatHalf2);
				break;
			default:
				checkNoEntry();
				break;
		}
		
		GeometryDescriptor->setOpaque(Segment.bForceOpaque);
		GeometryDescriptor->setTriangleCount((Segment.bEnabled) ? Segment.NumPrimitives : 0);
		GeometryDescriptor->setAllowDuplicateIntersectionFunctionInvocation(Segment.bAllowDuplicateAnyHitShaderInvocation);

		// Index Buffer Infos
		if (IndexBuffer != nullptr)
		{
			FMetalBufferPtr IndexBufferRes = IndexBuffer->GetCurrentBufferOrNull();
			
			// Metal does not provide the same size for an AS without an index buffer, so provide a dummy, size doesn't matter
			if(!IndexBufferRes)
			{
				IndexBufferRes = Device.DummyIndexBuffer;
			}
			
			GeometryDescriptor->setIndexType(IndexBuffer->GetIndexType());
			GeometryDescriptor->setIndexBuffer(IndexBufferRes ? IndexBufferRes->GetMTLBuffer() : nullptr);
			
			const uint32 IndexStride = IndexBuffer ? IndexBuffer->GetStride() : 0;
			uint32_t IndexBufferOffset = Initializer.IndexBufferOffset + IndexStride * Segment.FirstPrimitive * FMetalRayTracingGeometry::IndicesPerPrimitive;
			GeometryDescriptor->setIndexBufferOffset(IndexBufferRes ? IndexBufferRes->GetOffset() + IndexBufferOffset : 0);
		}

		const FMetalBufferPtr VertexBufferRes = VertexBuffer->GetCurrentBufferOrNull();

		GeometryDescriptor->setVertexBuffer(VertexBufferRes ? VertexBufferRes->GetMTLBuffer() : nullptr);
		GeometryDescriptor->setVertexBufferOffset(VertexBufferRes ? VertexBufferRes->GetOffset() + Segment.VertexBufferOffset : 0);
		GeometryDescriptor->setVertexStride(Segment.VertexBufferStride);

		GeometryDescriptors.Add(GeometryDescriptor);
		SegmentIndex++;
	}

	// Populate Acceleration Structure Descriptor.
	uint32 Usage = MTLAccelerationStructureUsageNone;

	if (Initializer.bAllowUpdate)
		Usage = MTL::AccelerationStructureUsageRefit;
	else if (Initializer.bFastBuild)
		Usage = MTL::AccelerationStructureUsagePreferFastBuild;

	AccelerationStructureDescriptor->setUsage(Usage);
	AccelerationStructureDescriptor->setGeometryDescriptors(NS::Array::alloc()->init((const NS::Object* const*)GeometryDescriptors.GetData(), GeometryDescriptors.Num()));
}

void ReleasePrimitiveAccelerationStructureGeometryDescriptors(MTL::PrimitiveAccelerationStructureDescriptor* Desc)
{
	// Because of lack of NS::MutableArray in Metal-cpp we need to manually free descriptors for all acceleration structures
	// instead of creating them on the stack when calculating size 
	NS::Array* GeometryDescriptors = Desc->geometryDescriptors();
	
	for(uint32 Idx = 0; Idx < GeometryDescriptors->count(); Idx++)
	{
		GeometryDescriptors->object(Idx)->release();	
	}
	
	GeometryDescriptors->release();	
}

static FRayTracingAccelerationStructureSize CalcRayTracingGeometrySize(FMetalDevice& Device, MTL::AccelerationStructureDescriptor* AccelerationStructureDescriptor)
{
	MTL::AccelerationStructureSizes DescriptorSize = Device.GetDevice()->accelerationStructureSizes(AccelerationStructureDescriptor);

	FRayTracingAccelerationStructureSize SizeInfo = {};
	SizeInfo.ResultSize = Align(DescriptorSize.accelerationStructureSize, GRHIRayTracingAccelerationStructureAlignment);
	SizeInfo.BuildScratchSize = Align(DescriptorSize.buildScratchBufferSize, GRHIRayTracingScratchBufferAlignment);
	SizeInfo.UpdateScratchSize = Align(FMath::Max(1u, (uint32)DescriptorSize.refitScratchBufferSize), GRHIRayTracingScratchBufferAlignment);
	
	return SizeInfo;
}

FRayTracingAccelerationStructureSize FMetalDynamicRHI::RHICalcRayTracingGeometrySize(const FRayTracingGeometryInitializer& Initializer)
{
    MTL_SCOPED_AUTORELEASE_POOL;
	
	MTL::PrimitiveAccelerationStructureDescriptor* AccelerationStructureDescriptor = MTL::PrimitiveAccelerationStructureDescriptor::descriptor();
	FillPrimitiveAccelerationStructureDesc(*Device, AccelerationStructureDescriptor, Initializer);

	FRayTracingAccelerationStructureSize Ret = CalcRayTracingGeometrySize(*Device, AccelerationStructureDescriptor);
	
	ReleasePrimitiveAccelerationStructureGeometryDescriptors(AccelerationStructureDescriptor);
	return Ret;
}

FRayTracingAccelerationStructureSize FMetalDynamicRHI::RHICalcRayTracingSceneSize(const FRayTracingSceneInitializer& Initializer)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
    MTL::InstanceAccelerationStructureDescriptor* InstanceDescriptor = MTL::InstanceAccelerationStructureDescriptor::descriptor();
    InstanceDescriptor->setInstanceCount(Initializer.MaxNumInstances);

    return CalcRayTracingGeometrySize(*Device, InstanceDescriptor);
}

class FMetalRayTracingShaderBindingTable : public FRHIShaderBindingTable
{
public:
	FMetalRayTracingShaderBindingTable(FRHICommandListBase& RHICmdList, const FRayTracingShaderBindingTableInitializer& Initializer, FMetalDevice& InDevice)
		: FRHIShaderBindingTable(Initializer),
		Device(InDevice)
	{
		Lifetime = Initializer.Lifetime;
		HitGroupIndexingMode = Initializer.HitGroupIndexingMode;
		ShaderBindingMode = Initializer.ShaderBindingMode;
		NumShaderSlotsPerGeometrySegment = Initializer.NumShaderSlotsPerGeometrySegment;
		
		if (EnumHasAnyFlags(Initializer.ShaderBindingMode, ERayTracingShaderBindingMode::Inline) && Initializer.NumGeometrySegments > 0)
		{
			// Doesn't make sense to have inline SBT without hitgroup indexing
			check(Initializer.HitGroupIndexingMode == ERayTracingHitGroupIndexingMode::Allow);

			const uint32 RecordBufferSize = FMath::Max(1u, Initializer.NumGeometrySegments) * sizeof(FMetalHitGroupSystemParameters);
			InlineGeometryParameterData.SetNumUninitialized(RecordBufferSize);
		}
	}
	
	void Commit(FMetalRHICommandContext* Context, FRHIBuffer* InlineBindingDataBuffer)
	{
		if (EnumHasAnyFlags(ShaderBindingMode, ERayTracingShaderBindingMode::Inline))
		{			
			if (InlineBindingDataBuffer)
			{
				TRHICommandList_RecursiveHazardous<FMetalRHICommandContext> RHICmdList(Context);

				const uint32 ParameterBufferSize = InlineGeometryParameterData.Num();
				FMetalRHIBuffer* Buffer = (FMetalRHIBuffer*)InlineBindingDataBuffer;
				
				FMetalBufferPtr TempBuffer = Device.GetResourceHeap().CreateBuffer(ParameterBufferSize, BufferBackedLinearTextureOffsetAlignment, BUF_Dynamic, MTL::ResourceCPUCacheModeDefaultCache | MTL::ResourceStorageModeShared, true);

				void* Result = TempBuffer->Contents();				
				FMemory::Memcpy(Result, InlineGeometryParameterData.GetData(), ParameterBufferSize);
				
				Context->CopyFromBufferToBuffer(TempBuffer, 0, Buffer->GetCurrentBuffer(), 0, ParameterBufferSize);
				
				FMetalDynamicRHI::Get().DeferredDelete(TempBuffer);
			}
		}
	}
	
	void SetInlineGeometryParameters(uint32 SegmentIndex, const void* InData, uint32 InDataSize)
	{
		const uint32 WriteOffset = InDataSize * SegmentIndex;
		FMemory::Memcpy(&InlineGeometryParameterData[WriteOffset], InData, InDataSize);
	}
	
	virtual FRHISizeAndStride GetInlineBindingDataSizeAndStride() const override
	{
		return FRHISizeAndStride { (uint64)InlineGeometryParameterData.Num(), sizeof(FMetalHitGroupSystemParameters) };
	}
	
	uint32 GetInlineRecordIndex(uint32 RecordIndex) const
	{	
		// Only care about shader slot 0 for inline geometry parameters -> remap the record index
		return (RecordIndex % NumShaderSlotsPerGeometrySegment == 0) ? RecordIndex / NumShaderSlotsPerGeometrySegment : INDEX_NONE;
	}
	
	uint32 GetNumShaderSlotsPerGeometrySegment() { return NumShaderSlotsPerGeometrySegment; }
	ERayTracingShaderBindingTableLifetime GetLifetime() { return Lifetime; }
	
	// Ray tracing shader bindings can be processed in parallel.
	// Each concurrent worker gets its own dedicated descriptor cache instance to avoid contention or locking.
	// Scaling beyond 5 total threads does not yield any speedup in practice.
	static constexpr uint32 MaxBindingWorkers = 5; // RHI thread + 4 parallel workers.
private:
	ERayTracingShaderBindingTableLifetime Lifetime = ERayTracingShaderBindingTableLifetime::Transient;
	ERayTracingHitGroupIndexingMode HitGroupIndexingMode = ERayTracingHitGroupIndexingMode::Allow;
	ERayTracingShaderBindingMode ShaderBindingMode = ERayTracingShaderBindingMode::RTPSO;
	
	TArray<uint8> InlineGeometryParameterData;
	uint32 NumShaderSlotsPerGeometrySegment = 0;
	FMetalDevice& Device;
};

FMetalRayTracingGeometry::FMetalRayTracingGeometry(FMetalDevice& InDevice, FRHICommandListBase& RHICmdList, const FRayTracingGeometryInitializer& InInitializer)
	: FRHIRayTracingGeometry(InInitializer),
	bHasPendingCompactionRequests(false),
	Device(InDevice)
{
	uint32 IndexBufferStride = 0;

	if (Initializer.IndexBuffer)
	{
		// In case index buffer in initializer is not yet in valid state during streaming we assume the geometry is using UINT32 format.
		IndexBufferStride = Initializer.IndexBuffer->GetSize() > 0
			? Initializer.IndexBuffer->GetStride()
			: 4;
	}
	checkf(!Initializer.IndexBuffer || (IndexBufferStride == 2 || IndexBufferStride == 4), TEXT("Index buffer must be 16 or 32 bit if in use."));

	RebuildDescriptors();

	// NOTE: We do not use the RHI API in order to avoid re-filling another descriptor.
	SizeInfo = CalcRayTracingGeometrySize(Device, AccelerationStructureDescriptor);
	
	AccelerationStructureIndex = 0;

	// If this RayTracingGeometry going to be used as streaming destination
	// we don't want to allocate its memory as it will be replaced later by streamed version
	// but we still need correct SizeInfo as it is used to estimate its memory requirements outside of RHI.
	if (Initializer.Type == ERayTracingGeometryInitializerType::StreamingDestination)
	{
		return;
	}

	FString DebugNameString = Initializer.DebugName.ToString();
	
	AccelerationStructure = Device.GetResourceHeap().CreateAccelerationStructure(SizeInfo.ResultSize);
	if(GetEmitDrawEvents())
	{
		AccelerationStructure->GetAccelerationStructure()->setLabel(FStringToNSString(DebugNameString));
	}
	check(AccelerationStructure);
}

FMetalRayTracingGeometry::~FMetalRayTracingGeometry()
{
	ReleaseUnderlyingResource();
	ReleaseBindlessHandles();
}

void FMetalRayTracingGeometry::ReleaseUnderlyingResource()
{
	RemoveCompactionRequest();
	
	if(AccelerationStructure)
	{
		FMetalDynamicRHI::Get().DeferredDelete(AccelerationStructure);
	}
	AccelerationStructure = nullptr;
	
	ReleaseDescriptors();
}

void FMetalRayTracingGeometry::ReleaseDescriptors()
{
	if(AccelerationStructureDescriptor)
	{
		ReleasePrimitiveAccelerationStructureGeometryDescriptors(AccelerationStructureDescriptor);
		AccelerationStructureDescriptor->release();
		AccelerationStructureDescriptor = nullptr;
	}
}

void FMetalRayTracingGeometry::Swap(FMetalRayTracingGeometry& Other)
{
	::Swap(AccelerationStructureDescriptor, Other.AccelerationStructureDescriptor);
	
	::Swap(AccelerationStructure, Other.AccelerationStructure);
	::Swap(AccelerationStructureIndex, Other.AccelerationStructureIndex);

	Initializer = Other.Initializer;

	SetupHitGroupSystemParameters();
	RebuildDescriptors();
	
	SizeInfo = CalcRayTracingGeometrySize(Device, AccelerationStructureDescriptor);
}

void FMetalRayTracingGeometry::RemoveCompactionRequest()
{
	if (bHasPendingCompactionRequests)
	{
		check(GetAccelerationStructure());
		bool bRequestFound = Device.GetRayTracingCompactionRequestHandler()->ReleaseRequest(this);
		bHasPendingCompactionRequests = false;
	}
}

void FMetalRayTracingGeometry::RebuildDescriptors()
{
	if(AccelerationStructureDescriptor)
	{
		ReleaseDescriptors();
	}
	
	AccelerationStructureDescriptor = MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
	FillPrimitiveAccelerationStructureDesc(Device, AccelerationStructureDescriptor, Initializer);
}

static void SetRayTracingHitGroup(
	FMetalRayTracingShaderBindingTable* ShaderTable, uint32 RecordIndex,
	const FMetalRayTracingGeometry* Geometry, uint32 GeometrySegmentIndex)
{	
	uint32 InlineRecordIndex = ShaderTable->GetInlineRecordIndex(RecordIndex);
	
	if(InlineRecordIndex != INDEX_NONE)
	{
		ShaderTable->SetInlineGeometryParameters(InlineRecordIndex, &Geometry->HitGroupSystemParameters[GeometrySegmentIndex], sizeof(FMetalHitGroupSystemParameters));
	}
}

void FMetalRHICommandContext::RHICommitShaderBindingTable(FRHIShaderBindingTable* InSBT, FRHIBuffer* InlineBindingDataBuffer)
{
	FMetalRayTracingShaderBindingTable* SBT = ResourceCast(InSBT);
	
	SBT->Commit(this, InlineBindingDataBuffer);
	
	FMetalRHIBuffer* Buffer = (FMetalRHIBuffer*)InlineBindingDataBuffer;
	StateCache.CacheOrSkipResourceResidencyUpdate(Buffer->GetCurrentBuffer()->GetMTLBuffer(), EMetalShaderStages::Compute, false);
}

void FMetalRHICommandContext::RHISetBindingsOnShaderBindingTable(FRHIShaderBindingTable* InSBT,
	FRHIRayTracingPipelineState* InPipeline,
	uint32 NumBindings, const FRayTracingLocalShaderBindings* Bindings,
	ERayTracingBindingType BindingType)
{
	FMetalRayTracingShaderBindingTable* ShaderTable = ResourceCast(InSBT);

	FGraphEventArray TaskList;

	const uint32 NumWorkerThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
	const uint32 MaxTasks = FApp::ShouldUseThreadingForPerformance() ? FMath::Min<uint32>(NumWorkerThreads, FMetalRayTracingShaderBindingTable::MaxBindingWorkers) : 1;

	struct FTaskContext
	{
		uint32 WorkerIndex = 0;
	};

	TArray<FTaskContext, TInlineAllocator<FMetalRayTracingShaderBindingTable::MaxBindingWorkers>> TaskContexts;
	for (uint32 WorkerIndex = 0; WorkerIndex < MaxTasks; ++WorkerIndex)
	{
		TaskContexts.Add(FTaskContext{ WorkerIndex });
	}

	FCriticalSection CacheLock;
	
	auto BindingTask = [this, Bindings, ShaderTable, BindingType, &CacheLock](const FTaskContext& Context, int32 CurrentIndex)
	{
		const FRayTracingLocalShaderBindings& Binding = Bindings[CurrentIndex];

		TArray<MTL::Resource*, TInlineAllocator<256>> UsedResources;
		
		if (BindingType == ERayTracingBindingType::HitGroup)
		{
			const FMetalRayTracingGeometry* Geometry = static_cast<const FMetalRayTracingGeometry*>(Binding.Geometry);

			if (Binding.BindingType != ERayTracingLocalShaderBindingType::Clear)
			{
				UsedResources.Add(Geometry->GetAccelerationStructure()->GetAccelerationStructure().get());
				
				FMetalRHIBuffer* IndexBuffer = ResourceCast(Geometry->Initializer.IndexBuffer.GetReference());
				if(IndexBuffer)
				{
					UsedResources.Add(IndexBuffer->GetCurrentBuffer()->GetMTLBuffer());
				}
				
				for (const FRayTracingGeometrySegment& Segment : Geometry->Initializer.Segments)
				{
					FMetalRHIBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
					UsedResources.Add(VertexBuffer->GetCurrentBuffer()->GetMTLBuffer());	
				}
				
				SetRayTracingHitGroup(
					ShaderTable, Binding.RecordIndex,
					Geometry, Binding.SegmentIndex);
				
				{
					FScopeLock ScopeLock(&CacheLock);
					for(MTL::Resource* Resource : UsedResources)
					{
						StateCache.CacheOrSkipResourceResidencyUpdate(Resource, EMetalShaderStages::Compute, true);
					}
				}
			}
			else
			{
				check(ShaderTable->GetLifetime() == ERayTracingShaderBindingTableLifetime::Transient);
			}
		}
		else if (BindingType == ERayTracingBindingType::CallableShader)
		{
			checkNoEntry();
		}
		else if (BindingType == ERayTracingBindingType::MissShader)
		{
			checkNoEntry();
		}
		else
		{
			checkNoEntry();
		}
	};

	// One helper worker task will be created at most per this many work items, plus one worker for current thread (unless running on a task thread),
	// up to a hard maximum of FMetalRayTracingShaderTable::MaxBindingWorkers.
	// Internally, parallel for tasks still subdivide the work into smaller chunks and perform fine-grained load-balancing.
	const int32 ItemsPerTask = 1024;

	ParallelForWithExistingTaskContext(TEXT("SetRayTracingBindings"), MakeArrayView(TaskContexts), NumBindings, ItemsPerTask, BindingTask);
}

void FMetalRayTracingGeometry::SetAccelerationStructure(FMetalAccelerationStructure* InBuffer)
{
	if(AccelerationStructure)
	{
		FMetalDynamicRHI::Get().DeferredDelete(AccelerationStructure);
	}
	AccelerationStructure = InBuffer;
}

void FMetalRayTracingGeometry::SetupHitGroupSystemParameters()
{
	const bool bIsTriangles = (Initializer.GeometryType == RTGT_Triangles);

	FMetalBindlessDescriptorManager* BindlessDescriptorManager = Device.GetBindlessDescriptorManager();
	auto GetBindlessHandle = [BindlessDescriptorManager](FMetalRHIBuffer* Buffer, uint32 ExtraOffset)
	{
		if (Buffer)
		{
			FRHIDescriptorHandle BindlessHandle = BindlessDescriptorManager->AllocateDescriptor(ERHIDescriptorType::BufferSRV, Buffer->GetCurrentBuffer().Get(), ExtraOffset);	
			return BindlessHandle;
		}
		return FRHIDescriptorHandle();
	};

	ReleaseBindlessHandles();

	HitGroupSystemParameters.Reset(Initializer.Segments.Num());

	FMetalRHIBuffer* IndexBuffer = ResourceCast(Initializer.IndexBuffer.GetReference());
	const uint32 IndexStride = IndexBuffer ? IndexBuffer->GetStride() : 0;
	
	if(IndexBuffer)
	{
		HitGroupSystemIndexView = GetBindlessHandle(IndexBuffer, 0);
		check(HitGroupSystemIndexView.GetIndex());
	}
	
	for (const FRayTracingGeometrySegment& Segment : Initializer.Segments)
	{
		FMetalRHIBuffer* VertexBuffer = ResourceCast(Segment.VertexBuffer.GetReference());
		const FRHIDescriptorHandle VBHandle = GetBindlessHandle(VertexBuffer, Segment.VertexBufferOffset);
		HitGroupSystemVertexViews.Add(VBHandle);

		FMetalHitGroupSystemParameters& SystemParameters = HitGroupSystemParameters.AddZeroed_GetRef();
		SystemParameters.RootConstants.SetVertexAndIndexStride(Segment.VertexBufferStride, IndexStride);
		
		SystemParameters.BindlessHitGroupSystemVertexBuffer = VBHandle.GetIndex();
		check(SystemParameters.BindlessHitGroupSystemVertexBuffer);
		
		if (bIsTriangles && (IndexBuffer != nullptr))
		{
			SystemParameters.BindlessHitGroupSystemIndexBuffer = HitGroupSystemIndexView.GetIndex();
			SystemParameters.RootConstants.IndexBufferOffsetInBytes = Initializer.IndexBufferOffset + IndexStride * Segment.FirstPrimitive * FMetalRayTracingGeometry::IndicesPerPrimitive;
			SystemParameters.RootConstants.FirstPrimitive = Segment.FirstPrimitive;
		}
	}
}

void FMetalRayTracingGeometry::ReleaseBindlessHandles()
{
	FMetalBindlessDescriptorManager* BindlessDescriptorManager = Device.GetBindlessDescriptorManager();

	for (FRHIDescriptorHandle BindlesHandle : HitGroupSystemVertexViews)
	{
		FMetalDynamicRHI::Get().DeferredDelete(BindlesHandle);
	}
	HitGroupSystemVertexViews.Reset(Initializer.Segments.Num());

	if (HitGroupSystemIndexView.IsValid())
	{
		FMetalDynamicRHI::Get().DeferredDelete(HitGroupSystemIndexView);
		HitGroupSystemIndexView = {};
	}
}

FMetalRayTracingScene::FMetalRayTracingScene(FMetalDevice& InDevice, FRayTracingSceneInitializer InInitializer)
	: Device(InDevice),
	Initializer(MoveTemp(InInitializer))
{
	MTL::InstanceAccelerationStructureDescriptor* InstanceDescriptor = MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
	InstanceDescriptor->setInstanceCount(Initializer.MaxNumInstances);

	SizeInfo = CalcRayTracingGeometrySize(Device, InstanceDescriptor);
	
	InstanceDescriptor->release();
}

FMetalRayTracingScene::~FMetalRayTracingScene()
{
}

void FMetalRayTracingScene::BindBuffer(FRHIBuffer* InBuffer, uint32 InBufferOffset)
{
	check(IsInRHIThread() || !IsRunningRHIInSeparateThread());
	check(SizeInfo.ResultSize + InBufferOffset <= InBuffer->GetSize());

	AccelerationStructureBuffer = ResourceCast(InBuffer);
	
	check(AccelerationStructureBuffer->IsAccelerationStructure());
	
	check(InBufferOffset % GRHIRayTracingAccelerationStructureAlignment == 0);
	
	if(GetEmitDrawEvents())
	{
		GetAccelerationStructure()->GetAccelerationStructure()->setLabel(FStringToNSString(Initializer.DebugName.ToString()));
	}
}

FRayTracingAccelerationStructureAddress FMetalRayTracingGeometry::GetAccelerationStructureAddress(uint64 GPUIndex) const
{
	return AccelerationStructure->GetAccelerationStructure()->gpuResourceID()._impl; 
}

void FMetalRHICommandContext::BuildAccelerationStructure(FMetalBufferPtr CurInstanceBuffer, uint32 InstanceBufferOffset, 
														 FMetalBufferPtr ScratchBuffer, uint32 ScratchBufferOffset,
														 FMetalBufferPtr HitGroupContributionsBuffer, uint32 HitGroupContributionsBufferOffset, 
														 uint32 MaxNumInstances, FMetalAccelerationStructure* AS)
{
	if (!CurrentEncoder.GetCommandBuffer())
	{
		StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	if(!CurrentEncoder.IsAccelerationStructureCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		
		// TODO: Carl - Sample Counters
		CurrentEncoder.BeginAccelerationStructureCommandEncoding();
	}

	MTL::AccelerationStructureCommandEncoder* CommandEncoder = CurrentEncoder.GetAccelerationStructureCommandEncoder();
	
	MTL::InstanceAccelerationStructureDescriptor* InstanceDescriptor = MTL::InstanceAccelerationStructureDescriptor::descriptor();
	InstanceDescriptor->setInstanceCount(MaxNumInstances);
	InstanceDescriptor->setInstanceDescriptorBuffer(CurInstanceBuffer->GetMTLBuffer());
	InstanceDescriptor->setInstanceDescriptorBufferOffset(InstanceBufferOffset);
	InstanceDescriptor->setInstanceDescriptorStride(GRHIRayTracingInstanceDescriptorSize);
	InstanceDescriptor->setInstanceDescriptorType(MTL::AccelerationStructureInstanceDescriptorTypeIndirect);

	FMetalBufferPtr IndirectArgs = AS->GetIndirectArgumentBuffer();
	MTLAccelerationStructurePtr NativeAS = AS->GetAccelerationStructure();

	IRRaytracingAccelerationStructureGPUHeader* Header = (IRRaytracingAccelerationStructureGPUHeader*)IndirectArgs->Contents();
	memset(Header, 0, sizeof(IRRaytracingAccelerationStructureGPUHeader));
	
	Header->accelerationStructureID = NativeAS->gpuResourceID()._impl;
	Header->addressOfInstanceContributions = (uint64_t)HitGroupContributionsBuffer->GetGPUAddress() + HitGroupContributionsBufferOffset;

	check(Header->addressOfInstanceContributions);
	CommandEncoder->buildAccelerationStructure(NativeAS.get(), InstanceDescriptor, ScratchBuffer->GetMTLBuffer(), ScratchBufferOffset);
}

void FMetalRHICommandContext::BuildAccelerationStructure(MTLAccelerationStructurePtr AS, MTL::AccelerationStructureDescriptor* Descriptor,
														 FMetalBufferPtr ScratchBuffer, uint32 ScratchBufferOffset)
{
	if (!CurrentEncoder.GetCommandBuffer())
	{
		StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	if(!CurrentEncoder.IsAccelerationStructureCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		
		// TODO: Carl - Sample Counters
		CurrentEncoder.BeginAccelerationStructureCommandEncoding();
	}

	MTL::AccelerationStructureCommandEncoder* CommandEncoder = CurrentEncoder.GetAccelerationStructureCommandEncoder();

	CommandEncoder->buildAccelerationStructure(AS.get(), Descriptor, ScratchBuffer->GetMTLBuffer(), ScratchBufferOffset);
}

void FMetalRHICommandContext::RefitAccelerationStructure(MTLAccelerationStructurePtr SrcBLAS, MTLAccelerationStructurePtr DestBLAS, MTL::PrimitiveAccelerationStructureDescriptor* Descriptor, MTL::Buffer* ScratchBuffer, uint32 ScratchOffset)
{
	if (!CurrentEncoder.GetCommandBuffer())
	{
		StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
	
	if(!CurrentEncoder.IsAccelerationStructureCommandEncoderActive())
	{
		StateCache.ClearPreviousComputeState();
		if(CurrentEncoder.IsAnyCommandEncoderActive())
		{
			CurrentEncoderFence = CurrentEncoder.EndEncoding();
		}
		
		// TODO: Carl - Sample Counters
		CurrentEncoder.BeginAccelerationStructureCommandEncoding();
	}

	MTL::AccelerationStructureCommandEncoder* CommandEncoder = CurrentEncoder.GetAccelerationStructureCommandEncoder();
	CommandEncoder->refitAccelerationStructure(SrcBLAS.get(), Descriptor, DestBLAS.get(), ScratchBuffer, ScratchOffset);
}

void FMetalRayTracingScene::BuildAccelerationStructure(FMetalRHICommandContext& CommandContext,
														FMetalRHIBuffer* InScratchBuffer, uint32 ScratchOffset,
														FMetalRHIBuffer* InstanceBuffer, uint32 InstanceOffset,
													    FMetalRHIBuffer* HitGroupContributionsBuffer, uint32 HitGroupContributionOffset, 
													    uint32 NumInstances)
{
	check(InstanceBuffer != nullptr);
	check(HitGroupContributionsBuffer != nullptr);

	FMetalBufferPtr CurInstanceBuffer = InstanceBuffer->GetCurrentBuffer();
	FMetalBufferPtr CurHitGroupContributionsBuffer = HitGroupContributionsBuffer->GetCurrentBuffer();
	check(CurInstanceBuffer);

	uint32 InstanceBufferOffset = InstanceOffset + static_cast<uint32>(CurInstanceBuffer->GetOffset());

	TRefCountPtr<FMetalRHIBuffer> ScratchBuffer;
	if (InScratchBuffer == nullptr)
	{
		{
			TRHICommandList_RecursiveHazardous<FMetalRHICommandContext> RHICmdList(&CommandContext);
			
			const FRHIBufferCreateDesc CreateDesc = FRHIBufferCreateDesc::CreateStructured(TEXT("BuildScratchTLAS"), SizeInfo.BuildScratchSize, 0)
													.AddUsage(EBufferUsageFlags::UnorderedAccess)
													.SetInitialState(ERHIAccess::UAVCompute);
			
			ScratchBuffer = ResourceCast(RHICmdList.CreateBuffer(CreateDesc).GetReference());
			InScratchBuffer = ScratchBuffer.GetReference();
			ScratchOffset = 0;
		}
	}
	
	// Create new Indirect Args buffer
	FMetalBufferPtr IndirectArgumentBuffer = Device.GetResourceHeap().CreateBuffer(sizeof(IRRaytracingAccelerationStructureGPUHeader),
																   sizeof(IRRaytracingAccelerationStructureGPUHeader),
																   BUF_Dynamic,
																   MTL::ResourceCPUCacheModeDefaultCache | MTL::ResourceStorageModeShared,
																   true);
	
	
	GetAccelerationStructure()->SetIndirectArgumentBuffer(IndirectArgumentBuffer);
	AccelerationStructureBuffer->UpdateLinkedViews(&CommandContext);
	
	FMetalBufferPtr CurScratchBuffer = InScratchBuffer->GetCurrentBuffer();
	check(CurScratchBuffer);

	CommandContext.BuildAccelerationStructure(CurInstanceBuffer, InstanceBufferOffset, CurScratchBuffer, CurScratchBuffer->GetOffset() + ScratchOffset,
											  CurHitGroupContributionsBuffer, HitGroupContributionOffset, NumInstances, GetAccelerationStructure());
}

void FMetalRHICommandContext::RHIBuildAccelerationStructures(TConstArrayView<FRayTracingSceneBuildParams> Params)
{
	for (const FRayTracingSceneBuildParams& SceneBuildParams : Params)
	{
		FMetalRayTracingScene* const Scene = ResourceCast(SceneBuildParams.Scene);
		FMetalRHIBuffer* const ScratchBuffer = ResourceCast(SceneBuildParams.ScratchBuffer);
		FMetalRHIBuffer* const InstanceBuffer = ResourceCast(SceneBuildParams.InstanceBuffer);
		FMetalRHIBuffer* const HitGroupContributionsBuffer = ResourceCast(SceneBuildParams.HitGroupContributionsBuffer); 
		
		Scene->ReferencedGeometries.Reserve(SceneBuildParams.ReferencedGeometries.Num());

		for (FRHIRayTracingGeometry* ReferencedGeometry : SceneBuildParams.ReferencedGeometries)
		{
			Scene->ReferencedGeometries.Add(ReferencedGeometry);
		}
		
		Scene->BuildAccelerationStructure(
			*this,
			ScratchBuffer, SceneBuildParams.ScratchBufferOffset,
			InstanceBuffer, SceneBuildParams.InstanceBufferOffset,
			HitGroupContributionsBuffer, SceneBuildParams.HitGroupContributionsBufferOffset,
			SceneBuildParams.NumInstances);
	}
}

void FMetalRHICommandContext::RHIBuildAccelerationStructures(TConstArrayView<FRayTracingGeometryBuildParams> Params, const FRHIBufferRange& ScratchBufferRange)
{
	checkf(ScratchBufferRange.Buffer != nullptr, TEXT("BuildAccelerationStructures requires valid scratch buffer"));

	// Update geometry vertex buffers
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FMetalRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());

		if (P.Segments.Num())
		{
			checkf(P.Segments.Num() == Geometry->Initializer.Segments.Num(),
				TEXT("If updated segments are provided, they must exactly match existing geometry segments. Only vertex buffer bindings may change."));

			for (int32 i = 0; i < P.Segments.Num(); ++i)
			{
				checkf(P.Segments[i].VertexBuffer != nullptr, TEXT("Segments used to build/update ray tracing geometry must have a valid VertexBuffer."));

				checkf(P.Segments[i].MaxVertices <= Geometry->Initializer.Segments[i].MaxVertices,
					TEXT("Maximum number of vertices in a segment (%u) must not be smaller than what was declared during FRHIRayTracingGeometry creation (%u), as this controls BLAS memory allocation."),
					P.Segments[i].MaxVertices, Geometry->Initializer.Segments[i].MaxVertices
				);

				Geometry->Initializer.Segments[i].VertexBuffer = P.Segments[i].VertexBuffer;
				Geometry->Initializer.Segments[i].VertexBufferElementType = P.Segments[i].VertexBufferElementType;
				Geometry->Initializer.Segments[i].VertexBufferStride = P.Segments[i].VertexBufferStride;
				Geometry->Initializer.Segments[i].VertexBufferOffset = P.Segments[i].VertexBufferOffset;
			}
		}
	}

	uint32 ScratchBufferSize = ScratchBufferRange.Size ? ScratchBufferRange.Size : ScratchBufferRange.Buffer->GetSize();

	checkf(ScratchBufferSize + ScratchBufferRange.Offset <= ScratchBufferRange.Buffer->GetSize(),
		TEXT("BLAS scratch buffer range size is %lld bytes with offset %lld, but the buffer only has %lld bytes. "),
		ScratchBufferRange.Size, ScratchBufferRange.Offset, ScratchBufferRange.Buffer->GetSize());

	const uint64 ScratchAlignment = GRHIRayTracingScratchBufferAlignment;
	FMetalRHIBuffer* ScratchBuffer = ResourceCast(ScratchBufferRange.Buffer);
	uint32 ScratchBufferOffset = static_cast<uint32>(ScratchBufferRange.Offset);

	struct FGeometryBuildData
	{
		FMetalRayTracingGeometry* Geometry;
		uint32 Offset;
	};
	
	TArray<FGeometryBuildData, TInlineAllocator<32>> GeometryToBuild;
	TArray<FGeometryBuildData, TInlineAllocator<32>> GeometryToRefit;
	GeometryToBuild.Reserve(Params.Num());
	GeometryToRefit.Reserve(Params.Num());
	
	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FMetalRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());
		const bool bIsUpdate = P.BuildMode == EAccelerationStructureBuildMode::Update;

		uint64 ScratchBufferRequiredSize = bIsUpdate ? Geometry->SizeInfo.UpdateScratchSize : Geometry->SizeInfo.BuildScratchSize;
		checkf(ScratchBufferRequiredSize + ScratchBufferOffset <= ScratchBufferSize,
			TEXT("BLAS scratch buffer size is %ld bytes with offset %ld (%ld bytes available), but the build requires %lld bytes. "),
			ScratchBufferSize, ScratchBufferOffset, ScratchBufferSize - ScratchBufferOffset, ScratchBufferRequiredSize);

		if (!bIsUpdate)
		{
			GeometryToBuild.Add({Geometry, ScratchBufferOffset});
		}
		else
		{
			GeometryToRefit.Add({Geometry, ScratchBufferOffset});
		}

		ScratchBufferOffset = Align(ScratchBufferOffset + ScratchBufferRequiredSize, ScratchAlignment);

		// We must update the descriptor if any segments have changed.
		Geometry->RebuildDescriptors();
		
		// TODO: Add a CVAR to toggle validation (e.g. r.Metal.RayTracingValidate).
		//FRayTracingValidateGeometryBuildParamsCS::Dispatch(FRHICommandListExecutor::GetImmediateCommandList(), P);
	}

	FMetalBufferPtr ScratchBufferRes = ScratchBuffer->GetCurrentBuffer();
	check(ScratchBufferRes);

	for (FGeometryBuildData& BuildRequest : GeometryToBuild)
	{
		FMetalRayTracingGeometry* Geometry = BuildRequest.Geometry;
		
		FRayTracingAccelerationStructureSize TestSize = CalcRayTracingGeometrySize(Device, Geometry->AccelerationStructureDescriptor);
																		
		BuildAccelerationStructure(Geometry->GetAccelerationStructure()->GetAccelerationStructure(), Geometry->AccelerationStructureDescriptor,
										   ScratchBufferRes, BuildRequest.Offset);
	}

	for (FGeometryBuildData& RefitRequest : GeometryToRefit)
	{
		FMetalRayTracingGeometry* Geometry = RefitRequest.Geometry;
		uint32 ScratchOffset = RefitRequest.Offset;
 
		FMetalAccelerationStructure* ReadStruct = Geometry->GetAccelerationStructure();
		
		MTLAccelerationStructurePtr SrcBLAS = ReadStruct->GetAccelerationStructure();
		MTLAccelerationStructurePtr DstBLAS = {};

		RefitAccelerationStructure(
			SrcBLAS,
			DstBLAS,
			Geometry->AccelerationStructureDescriptor,
			ScratchBufferRes->GetMTLBuffer(),
			ScratchOffset
		);
	}

	for (const FRayTracingGeometryBuildParams& P : Params)
	{
		FMetalRayTracingGeometry* const Geometry = ResourceCast(P.Geometry.GetReference());
		const bool bIsUpdate = P.BuildMode == EAccelerationStructureBuildMode::Update;

		if (!bIsUpdate)
		{
			ERayTracingAccelerationStructureFlags GeometryBuildFlags = GetRayTracingAccelerationStructureBuildFlags(Geometry->Initializer);
			if (ShouldCompactAfterBuild(GeometryBuildFlags))
			{
				Device.GetRayTracingCompactionRequestHandler()->RequestCompact(Geometry);
				Geometry->bHasPendingCompactionRequests = true;
			}
		}
		
		Geometry->SetupHitGroupSystemParameters();
	}
}

void FMetalRHICommandContext::RHIBindAccelerationStructureMemory(FRHIRayTracingScene* InScene, FRHIBuffer* InBuffer, uint32 InBufferOffset)
{
	FMetalRayTracingScene* MetalScene = ResourceCast(InScene);
	MetalScene->BindBuffer(InBuffer, InBufferOffset);
}

void FMetalRHICommandContext::RHIClearShaderBindingTable(FRHIShaderBindingTable* SBT)
{
	// TODO:
}

void FMetalRHICommandContext::RHIRayTraceDispatch(FRHIRayTracingPipelineState* RayTracingPipelineState, FRHIRayTracingShader* RayGenShader,
												  FRHIShaderBindingTable* SBT, const FRayTracingShaderBindings& GlobalResourceBindings,
												  uint32 Width, uint32 Height)
{
	checkNoEntry();
}

void FMetalRHICommandContext::RHIRayTraceDispatchIndirect(FRHIRayTracingPipelineState* InRayTracingPipelineState, FRHIRayTracingShader* InRayGenShader,
														  FRHIShaderBindingTable* InSBT, const FRayTracingShaderBindings& GlobalResourceBindings,
														  FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	checkNoEntry();
}

FRayTracingSceneRHIRef FMetalDynamicRHI::RHICreateRayTracingScene(FRayTracingSceneInitializer Initializer)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    return new FMetalRayTracingScene(*Device, MoveTemp(Initializer));
}

FRayTracingGeometryRHIRef FMetalDynamicRHI::RHICreateRayTracingGeometry(FRHICommandListBase& RHICmdList, const FRayTracingGeometryInitializer& Initializer)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    return new FMetalRayTracingGeometry(*Device, RHICmdList, Initializer);
}

FRayTracingPipelineStateRHIRef FMetalDynamicRHI::RHICreateRayTracingPipelineState(const FRayTracingPipelineStateInitializer& Initializer)
{
	checkNoEntry();
	return nullptr;
}

FShaderBindingTableRHIRef FMetalDynamicRHI::RHICreateShaderBindingTable(FRHICommandListBase& RHICmdList, const FRayTracingShaderBindingTableInitializer& Initializer)
{
	return new FMetalRayTracingShaderBindingTable(RHICmdList, Initializer, *Device);
}

void FMetalDevice::InitializeRayTracing()
{
	// Explicitly request a pointer to the DeviceContext since the CompactionHandler
	// is initialized before the global getter is setup.
	RayTracingCompactionRequestHandler = new FMetalRayTracingCompactionRequestHandler(*this);
	
	DummyIndexBuffer = GetResourceHeap().CreateBuffer(64, 16, BUF_Static, MTL::ResourceCPUCacheModeDefaultCache | MTL::ResourceStorageModeShared, true);
}

void FMetalDevice::UpdateRayTracing(FMetalRHICommandContext& Context)
{
	RayTracingCompactionRequestHandler->Update(Context);
}

void FMetalDevice::CleanUpRayTracing()
{
	delete RayTracingCompactionRequestHandler;
}
#endif // METAL_RHI_RAYTRACING
