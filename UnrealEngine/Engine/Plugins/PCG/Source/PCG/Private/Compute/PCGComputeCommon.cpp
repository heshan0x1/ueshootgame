// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/PCGComputeCommon.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGSubsystem.h"
#include "Compute/PCGComputeGraph.h"
#include "Compute/PCGComputeKernel.h"
#include "Compute/PCGDataBinding.h"
#include "Compute/PCGPinPropertiesGPU.h"
#include "Compute/Data/PCGProxyForGPUData.h"
#include "Compute/Data/PCGRawBufferData.h"
#include "Compute/DataInterfaces/PCGDataCollectionDataInterface.h"
#include "Compute/DataInterfaces/PCGLandscapeDataInterface.h"
#include "Compute/DataInterfaces/PCGRawBufferDataInterface.h"
#include "Compute/DataInterfaces/PCGStaticMeshDataInterface.h"
#include "Compute/DataInterfaces/PCGTextureDataInterface.h"
#include "Compute/DataInterfaces/PCGVirtualTextureDataInterface.h"
#include "Compute/Packing/PCGDataCollectionPacking.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGTextureData.h"
#include "Graph/PCGGPUGraphCompilationContext.h"
#include "Editor/IPCGEditorModule.h"

#include "DynamicRHI.h"
#include "RHIStats.h"
#include "RenderResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGComputeCommon)

namespace PCGComputeConstants
{
	constexpr TCHAR DataLabelTagPrefix[] = { TEXT("PCG_DATA_LABEL") };
}

namespace PCGComputeHelpers
{
	static TAutoConsoleVariable<float> CVarMaxGPUBufferSizeProportion(
		TEXT("pcg.GraphExecution.GPU.MaxBufferSize"),
		0.5f,
		TEXT("Maximum GPU buffer size as proportion of total available graphics memory."));

	FIntVector4 GetElementCount(const UPCGData* InData)
	{
		FIntVector4 ElementCount = FIntVector4::ZeroValue;

		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(InData))
		{
			ElementCount.X = PointData->GetNumPoints();
		}
		else if (const UPCGParamData* ParamData = Cast<UPCGParamData>(InData))
		{
			if (const UPCGMetadata* Metadata = ParamData->ConstMetadata())
			{
				ElementCount.X = Metadata->GetItemCountForChild();
			}
		}
		else if (const UPCGProxyForGPUData* Proxy = Cast<UPCGProxyForGPUData>(InData))
		{
			ElementCount = Proxy->GetElementCount();
		}
		else if (const UPCGBaseTextureData* TextureData = Cast<UPCGBaseTextureData>(InData))
		{
			const FIntPoint TextureSize = TextureData->GetTextureSize();
			ElementCount.X = TextureSize.X;
			ElementCount.Y = TextureSize.Y;
		}
		else if (const UPCGRawBufferData* RawBufferData = Cast<UPCGRawBufferData>(InData))
		{
			ElementCount.X = RawBufferData->GetNumUint32s();
		}

		return ElementCount;
	}

	EPCGElementDimension GetElementDimension(const UPCGData* InData)
	{
		return InData ? GetElementDimension(InData->GetDataTypeId()) : EPCGElementDimension::One;
	}

	EPCGElementDimension GetElementDimension(const FPCGDataTypeIdentifier& InDataType)
	{
		return !!(InDataType & EPCGDataType::BaseTexture) ? EPCGElementDimension::Two : EPCGElementDimension::One;
	}

	const TArray<FPCGDataTypeIdentifier>& GetAllowedInputTypesList()
	{
		static TArray<FPCGDataTypeIdentifier> AllowedTypes =
		{
			EPCGDataType::Point,
			EPCGDataType::Param,
			EPCGDataType::Landscape,
			EPCGDataType::BaseTexture,
			EPCGDataType::VirtualTexture,
			EPCGDataType::StaticMeshResource,
			EPCGDataType::ProxyForGPU,
			FPCGDataTypeInfoRawBuffer::AsId()
		};

		return AllowedTypes;
	}

	const TArray<FPCGDataTypeIdentifier>& GetAllowedOutputTypesList()
	{
		static TArray<FPCGDataTypeIdentifier> AllowedTypes =
		{
			EPCGDataType::Point,
			EPCGDataType::Param,
			EPCGDataType::ProxyForGPU,
			EPCGDataType::BaseTexture,
			FPCGDataTypeInfoRawBuffer::AsId()
		};

		return AllowedTypes;
	}

	inline const FPCGDataTypeIdentifier& GetAllowedInputTypes()
	{
		static FPCGDataTypeIdentifier Result = FPCGDataTypeIdentifier::Construct(GetAllowedInputTypesList());
		return Result;
	}

	inline const FPCGDataTypeIdentifier& GetAllowedOutputTypes()
	{
		static FPCGDataTypeIdentifier Result = FPCGDataTypeIdentifier::Construct(GetAllowedOutputTypesList());
		return Result;
	}

	/** PCG data types supported in GPU data collections. */
	inline const FPCGDataTypeIdentifier& GetAllowedDataCollectionTypes()
	{
		// Intentionally excludes raw buffer data which cannot be mixed with other data types like point.
		static FPCGDataTypeIdentifier Result{ EPCGDataType::Point | EPCGDataType::Param | EPCGDataType::ProxyForGPU };
		return Result;
	}

	bool IsTypeAllowedAsInput(const FPCGDataTypeIdentifier& Type)
	{
		return GetAllowedInputTypes().Intersects(Type);
	}

	bool IsTypeAllowedAsOutput(const FPCGDataTypeIdentifier& Type)
	{
		return GetAllowedOutputTypes().Intersects(Type);
	}

	bool IsTypeAllowedInDataCollection(const FPCGDataTypeIdentifier& Type)
	{
		return GetAllowedDataCollectionTypes().Intersects(Type);
	}

	bool ShouldImportAttributesFromData(const UPCGData* InData)
	{
		// We only read and expose attributes to compute from the following types. Other types are supported but we don't
		// register/upload their metadata attributes automatically.
		return InData && (InData->IsA<UPCGParamData>() || InData->IsA<UPCGBasePointData>());
	}

#if PCG_KERNEL_LOGGING_ENABLED
	void LogKernelWarning(const FPCGContext* Context, const UPCGSettings* Settings, const FText& InText)
	{
#if WITH_EDITOR
		if (Context && Settings)
		{
			if (IPCGEditorModule* PCGEditorModule = IPCGEditorModule::Get())
			{
				const FPCGStack* Stack = Context->GetStack();
				FPCGStack StackWithNode = Stack ? *Stack : FPCGStack();
				StackWithNode.PushFrame(Settings->GetOuter());

				PCGEditorModule->GetNodeVisualLogsMutable().Log(StackWithNode, ELogVerbosity::Warning, InText);
			}
		}
#endif
		PCGE_LOG_C(Warning, LogOnly, Context, InText);
	}

	void LogKernelError(const FPCGContext* Context, const UPCGSettings* Settings, const FText& InText)
	{
#if WITH_EDITOR
		if (Context && Settings)
		{
			if (IPCGEditorModule* PCGEditorModule = IPCGEditorModule::Get())
			{
				FPCGStack StackWithNode = Context->GetStack() ? *Context->GetStack() : FPCGStack();
				StackWithNode.PushFrame(Settings->GetOuter());

				PCGEditorModule->GetNodeVisualLogsMutable().Log(StackWithNode, ELogVerbosity::Error, InText);
			}
		}
#endif
		PCGE_LOG_C(Error, LogOnly, Context, InText);
	}
#endif

	bool IsBufferSizeTooLarge(uint64 InBufferSizeBytes, bool bInLogError)
	{
		FTextureMemoryStats TextureMemStats;
		RHIGetTextureMemoryStats(TextureMemStats);

		// If buffer size exceeds a proportion of total graphics memory, then it is deemed too large. Using this as a heuristic as there
		// is no RHI API to obtain available graphics memory outside of D3D12.
		const uint64 GPUBudgetBytes = static_cast<uint64>(TextureMemStats.TotalGraphicsMemory) * static_cast<double>(CVarMaxGPUBufferSizeProportion.GetValueOnAnyThread());

		// Buffer size also cannot exceed the max size of a uint32, otherwise the size will get truncated and other systems will break.
		// TODO: This limits the maximum number of points to around 46 million. Support uint64 max instead of uint32 to get up to 2 billion points.
		const uint64 MaxBufferSize = TNumericLimits<uint32>::Max();
		const uint64 BudgetBytes = FMath::Min(GPUBudgetBytes, MaxBufferSize);

		const bool bBufferTooLarge = TextureMemStats.TotalGraphicsMemory > 0 && InBufferSizeBytes > BudgetBytes;

		if (bBufferTooLarge && bInLogError)
		{
			UE_LOG(LogPCG, Error, TEXT("Attempted to allocate a GPU buffer of size %llu bytes which is larger than the safety threshold (%llu bytes). Compute graph execution aborted."),
				InBufferSizeBytes,
				BudgetBytes);
		}

		return bBufferTooLarge;
	}

	int32 GetAttributeIdFromMetadataAttributeIndex(int32 InAttributeIndex)
	{
		return InAttributeIndex >= 0 ? (InAttributeIndex + PCGDataCollectionPackingConstants::NUM_RESERVED_ATTRS) : INDEX_NONE;
	}

	int32 GetMetadataAttributeIndexFromAttributeId(int32 InAttributeId)
	{
		return InAttributeId >= PCGDataCollectionPackingConstants::NUM_RESERVED_ATTRS ? (InAttributeId - PCGDataCollectionPackingConstants::NUM_RESERVED_ATTRS) : INDEX_NONE;
	}

	FString GetPrefixedDataLabel(const FString& InLabel)
	{
		return FString::Format(TEXT("{0}:{1}"), { PCGComputeConstants::DataLabelTagPrefix, InLabel });
	}

	FString GetDataLabelResolverName(FName InPinLabel)
	{
		return FString::Format(TEXT("{0}_DataResolver"), { *InPinLabel.ToString() });
	}

#if WITH_EDITOR
	void ConvertObjectPathToShaderFilePath(FString& InOutPath)
	{
		// Shader compiler recognizes "/Engine/Generated/..." path as special. 
		// It doesn't validate file suffix etc.
		InOutPath = FString::Printf(TEXT("/Engine/Generated/UObject%s.ush"), *InOutPath);
		// Shader compilation result parsing will break if it finds ':' where it doesn't expect.
		InOutPath.ReplaceCharInline(TEXT(':'), TEXT('@'));
	}

	UPCGComputeDataInterface* CreateOutputPinDataInterface(const FCreateDataInterfaceParams& InParams)
	{
		check(InParams.Context);
		check(InParams.PinProperties);
		check(InParams.ObjectOuter);

		FPCGGPUCompilationContext& Context = *InParams.Context;
		const FPCGPinProperties& PinProperties = *InParams.PinProperties;

		UPCGComputeDataInterface* DataInterface = nullptr;

		if (PCGComputeHelpers::IsTypeAllowedInDataCollection(PinProperties.AllowedTypes))
		{
			UPCGDataCollectionDataInterface* DataInterfacePCGData = Context.NewObject_AnyThread<UPCGDataCollectionDataInterface>(InParams.ObjectOuter);

			// If data comes from a CPU task, upload it to the GPU.
			ensureMsgf(!InParams.bRequiresExport || !InParams.bProducedByCPU, TEXT("Download from GPU only relevant for data produced on GPU."));

			DataInterfacePCGData->SetRequiresExport(InParams.bRequiresExport);

			DataInterfacePCGData->SetElementCountMultiplier(InParams.ProducerKernel ? InParams.ProducerKernel->GetElementCountMultiplier(PinProperties.Label) : 1);
			DataInterfacePCGData->SetRequiresZeroInitialization(InParams.ProducerKernel && InParams.ProducerKernel->DoesOutputPinRequireZeroInitialization(PinProperties.Label));

			DataInterface = DataInterfacePCGData;
		}
		else if (PinProperties.AllowedTypes == EPCGDataType::VirtualTexture)
		{
			DataInterface = Context.NewObject_AnyThread<UPCGVirtualTextureDataInterface>(InParams.ObjectOuter);
		}
		else if (PinProperties.AllowedTypes.Intersects(EPCGDataType::BaseTexture))
		{
			UPCGTextureDataInterface* TextureDataInterface = Context.NewObject_AnyThread<UPCGTextureDataInterface>(InParams.ObjectOuter);
			TextureDataInterface->SetRequiresExport(InParams.bRequiresExport);
			TextureDataInterface->SetInitializeFromDataCollection(InParams.bProducedByCPU);

			DataInterface = TextureDataInterface;
		}
		else if (PinProperties.AllowedTypes == EPCGDataType::Landscape)
		{
			DataInterface = Context.NewObject_AnyThread<UPCGLandscapeDataInterface>(InParams.ObjectOuter);
		}
		else if (PinProperties.AllowedTypes == EPCGDataType::StaticMeshResource)
		{
			DataInterface = Context.NewObject_AnyThread<UPCGStaticMeshDataInterface>(InParams.ObjectOuter);
		}
		else if (PinProperties.AllowedTypes == FPCGDataTypeInfoRawBuffer::AsId())
		{
			UPCGRawBufferDataInterface* DataInterfaceRawBuffer = Context.NewObject_AnyThread<UPCGRawBufferDataInterface>(InParams.ObjectOuter);

			DataInterfaceRawBuffer->SetRequiresExport(InParams.bRequiresExport);
			DataInterfaceRawBuffer->SetRequiresZeroInitialization(InParams.ProducerKernel && InParams.ProducerKernel->DoesOutputPinRequireZeroInitialization(PinProperties.Label));

			// todo_pcg: We should support SetElementCountMultiplier also.

			DataInterface = DataInterfaceRawBuffer;
		}
		else
		{
			UE_LOG(LogPCG, Error, TEXT("Unsupported connected upstream pin '%s' on node '%s' with type %s. Consider adding a conversion to a supported type such as Point."),
				*PinProperties.Label.ToString(),
				InParams.NodeForDebug ? *InParams.NodeForDebug->GetNodeTitle(EPCGNodeTitleType::ListView).ToString() : TEXT("MISSING"),
				*PinProperties.AllowedTypes.ToString()
			);
		}

		return DataInterface;
	}

	static void NotifyHelper(UPCGDataBinding* InBinding, const UPCGComputeKernel* InKernel, const UPCGSettings* InSettings, const TFunction<void(FPCGContext*, const UPCGNode*)>& InLambda)
	{
		if (!ensure(InBinding))
		{
			return;
		}

		TSharedPtr<FPCGContextHandle> ContextHandle = InBinding->GetContextHandle().Pin();
		if (FPCGContext* Context = ContextHandle ? ContextHandle->GetContext() : nullptr)
		{
			const UPCGNode* IndicatorNode = nullptr;

			if (InKernel)
			{
				IndicatorNode = InBinding->GetComputeGraph() ? InBinding->GetComputeGraph()->GetKernelNode(InKernel) : nullptr;
			}
			else if (InSettings)
			{
				IndicatorNode = Cast<UPCGNode>(InSettings->GetOuter());
			}

			if (IndicatorNode)
			{
				InLambda(Context, IndicatorNode);
			}
		}
	}

	void NotifyGPUToCPUReadback(UPCGDataBinding* InBinding, const UPCGComputeKernel* InKernel, const UPCGSettings* InSettings)
	{
		NotifyHelper(InBinding, InKernel, InSettings, [](FPCGContext* InContext, const UPCGNode* InNode)
		{
			InContext->ExecutionSource->GetExecutionState().GetInspection().NotifyGPUToCPUReadback(InNode, InContext->GetStack());
		});
	}

	void NotifyCPUToGPUUpload(UPCGDataBinding* InBinding, const UPCGComputeKernel* InKernel, const UPCGSettings* InSettings)
	{
		NotifyHelper(InBinding, InKernel, InSettings, [](FPCGContext* InContext, const UPCGNode* InNode)
		{
			InContext->ExecutionSource->GetExecutionState().GetInspection().NotifyCPUToGPUUpload(InNode, InContext->GetStack());
		});
	}
#endif
}

namespace PCGComputeDummies
{
	class FPCGEmptyBufferSRV : public FRenderResource
	{
	public:
		FPCGEmptyBufferSRV(EPixelFormat InPixelFormat, const FString& InDebugName)
			: PixelFormat(InPixelFormat)
			, DebugName(InDebugName)
		{}

		EPixelFormat PixelFormat;
		FString DebugName;
		FBufferRHIRef Buffer = nullptr;
		FShaderResourceViewRHIRef BufferSRV = nullptr;
	
		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			// Create a buffer with one element.
			const uint32 NumBytes = GPixelFormats[PixelFormat].BlockBytes;

			FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateVertex(*DebugName, NumBytes)
				.AddUsage(EBufferUsageFlags::ShaderResource | EBufferUsageFlags::Static)
				.DetermineInitialState();

			Buffer = RHICmdList.CreateBuffer(CreateDesc.SetInitActionZeroData());
			BufferSRV = RHICmdList.CreateShaderResourceView(
				Buffer,
				FRHIViewDesc::CreateBufferSRV()
					.SetType(FRHIViewDesc::EBufferType::Typed)
					.SetFormat(PixelFormat));
		}
	
		virtual void ReleaseRHI() override
		{
			BufferSRV.SafeRelease();
			Buffer.SafeRelease();
		}
	};
	
	FRHIShaderResourceView* GetDummyFloatBuffer()
	{
		static TGlobalResource<FPCGEmptyBufferSRV> DummyFloatBuffer(PF_R32_FLOAT, TEXT("PCGDummyFloat"));
		return DummyFloatBuffer.BufferSRV;
	}
	
	FRHIShaderResourceView* GetDummyFloat2Buffer()
	{
		static TGlobalResource<FPCGEmptyBufferSRV> DummyFloat2Buffer(PF_G32R32F, TEXT("PCGDummyFloat2"));
		return DummyFloat2Buffer.BufferSRV;
	}
	
	FRHIShaderResourceView* GetDummyFloat4Buffer()
	{
		static TGlobalResource<FPCGEmptyBufferSRV> DummyFloat4Buffer(PF_A32B32G32R32F, TEXT("PCGDummyFloat4"));
		return DummyFloat4Buffer.BufferSRV;
	}
}

uint32 GetTypeHash(const FPCGPinReference& In)
{
	return HashCombine(/*GetTypeHash(In.TaskId),*/ PointerHash(In.Kernel), GetTypeHash(In.Label));
}

uint32 GetTypeHash(const FPCGKernelPin& In)
{
	return HashCombine(GetTypeHash(In.KernelIndex), GetTypeHash(In.PinLabel), GetTypeHash(In.bIsInput));
}

