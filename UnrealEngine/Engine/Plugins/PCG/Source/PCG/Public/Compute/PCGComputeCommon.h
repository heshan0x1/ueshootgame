// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "Data/Registry/PCGDataTypeIdentifier.h"

#include "PCGComputeCommon.generated.h"

class UPCGComputeDataInterface;
class UPCGComputeKernel;
class UPCGData;
class UPCGDataBinding;
class UPCGNode;
class UObject;
class UPCGSettings;
struct FPCGGPUCompilationContext;
struct FPCGContext;
struct FPCGPinProperties;

class FRHIShaderResourceView;

#define PCG_KERNEL_LOGGING_ENABLED (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || USE_LOGGING_IN_SHIPPING)

#if PCG_KERNEL_LOGGING_ENABLED
#define PCG_KERNEL_VALIDATION_WARN(Context, Settings, ValidationMessage) PCGComputeHelpers::LogKernelWarning(Context, Settings, ValidationMessage);
#define PCG_KERNEL_VALIDATION_ERR(Context, Settings, ValidationMessage) PCGComputeHelpers::LogKernelError(Context, Settings, ValidationMessage);
#else
#define PCG_KERNEL_VALIDATION_WARN(Context, Settings, ValidationMessage) // Log removed
#define PCG_KERNEL_VALIDATION_ERR(Context, Settings, ValidationMessage) // Log removed
#endif

/** Modes for exporting the buffer from transient to persistent for downstream consumption. */
UENUM(meta = (Bitflags))
enum class EPCGExportMode : uint8
{
	/** Buffer is transient and freed after usage. */
	NoExport = 0,
	/** Buffer will be exported and a proxy will be output from the compute graph and passed to downstream nodes. */
	ComputeGraphOutput = 1 << 0,
	/** Producer node is being inspected, read back data and store in inspection data. */
	Inspection = 1 << 1,
	/** Producer node is being debugged, read back data and execute debug visualization. */
	DebugVisualization = 1 << 2,
};
ENUM_CLASS_FLAGS(EPCGExportMode);

/** Dimensionality of a data description element count. */
UENUM()
enum class EPCGElementDimension : uint8
{
	One,
	Two,
	Three,
	Four
};

namespace PCGComputeConstants
{
	constexpr int32 MAX_PRIMITIVE_COMPONENTS_PER_SPAWNER = 256;
	constexpr uint32 THREAD_GROUP_SIZE = 64;
}

namespace PCGComputeHelpers
{
	/** Gets the element count for a given data. E.g. number of points in a PointData, number of metadata entries in a ParamData, etc. */
	FIntVector4 GetElementCount(const UPCGData* InData);

	/** Gets the element dimension for a given data. E.g. OneD for PointData, TwoD for TextureData, etc. */
	EPCGElementDimension GetElementDimension(const UPCGData* InData);
	EPCGElementDimension GetElementDimension(const FPCGDataTypeIdentifier& InDataType);

	/** PCG data types supported in GPU node inputs. */
	const TArray<FPCGDataTypeIdentifier>& GetAllowedInputTypesList();

	/** PCG data types supported in GPU node outputs. */
	const TArray<FPCGDataTypeIdentifier>& GetAllowedOutputTypesList();

	/** True if 'Type' is valid on a GPU input pin. */
	bool IsTypeAllowedAsInput(const FPCGDataTypeIdentifier& Type);

	/** True if 'Type' is valid on a GPU output pin. */
	bool IsTypeAllowedAsOutput(const FPCGDataTypeIdentifier& Type);

	/** True if 'Type' is valid in a GPU data collection. Some types are only supported as DataInterfaces, and cannot be uploaded in data collections. */
	bool IsTypeAllowedInDataCollection(const FPCGDataTypeIdentifier& Type);

	/** Whether metadata attributes should be read from the given data and registered for use in GPU graphs. */
	bool ShouldImportAttributesFromData(const UPCGData* InData);

#if PCG_KERNEL_LOGGING_ENABLED
	/** Logs a warning on a GPU node in the graph and console. */
	PCG_API void LogKernelWarning(const FPCGContext* Context, const UPCGSettings* Settings, const FText& InText);

	/** Logs an error on a GPU node in the graph and console. */
	PCG_API void LogKernelError(const FPCGContext* Context, const UPCGSettings* Settings, const FText& InText);
#endif

	/** Returns true if the given buffer size is dangerously large. Optionally emits error log. */
	bool IsBufferSizeTooLarge(uint64 InBufferSizeBytes, bool bInLogError = true);

	int32 GetAttributeIdFromMetadataAttributeIndex(int32 InAttributeIndex);
	int32 GetMetadataAttributeIndexFromAttributeId(int32 InAttributeId);

	/** Produces the data label prefixed with PCGComputeConstants::DataLabelTagPrefix. */
	FString GetPrefixedDataLabel(const FString& InLabel);

	/** Produces the data interface name of a data label resolver. */
	FString GetDataLabelResolverName(FName InPinLabel);

#if WITH_EDITOR
	void ConvertObjectPathToShaderFilePath(FString& InOutPath);

	struct FCreateDataInterfaceParams
	{
		FPCGGPUCompilationContext* Context = nullptr;
		const FPCGPinProperties* PinProperties = nullptr;
		const UPCGComputeKernel* ProducerKernel = nullptr;
		UObject* ObjectOuter = nullptr;
		bool bProducedByCPU = false;
		bool bRequiresExport = false;
		const UPCGNode* NodeForDebug = nullptr;
	};

	UPCGComputeDataInterface* CreateOutputPinDataInterface(const FCreateDataInterfaceParams& InParams);

	PCG_API void NotifyGPUToCPUReadback(UPCGDataBinding* InBinding, const UPCGComputeKernel* InKernel, const UPCGSettings* InSettings);
	PCG_API void NotifyCPUToGPUUpload(UPCGDataBinding* InBinding, const UPCGComputeKernel* InKernel, const UPCGSettings* InSettings);
#endif
}

namespace PCGComputeDummies
{
	FRHIShaderResourceView* GetDummyFloatBuffer();
	FRHIShaderResourceView* GetDummyFloat2Buffer();
	FRHIShaderResourceView* GetDummyFloat4Buffer();
}

/** A by-label reference to a pin, used for wiring kernels within a node. */
struct FPCGPinReference
{
	/** Reference a pin by label only, used for referencing node pins. */
	explicit FPCGPinReference(FName InLabel)
		: Kernel(nullptr)
		, Label(InLabel)
	{
	}

	/** Reference a pin by kernel and label. */
	explicit FPCGPinReference(UPCGComputeKernel* InKernel, FName InLabel)
		: Kernel(InKernel)
		, Label(InLabel)
	{
	}

	bool operator==(const FPCGPinReference& Other) const
	{
		return Label == Other.Label
			&& Kernel == Other.Kernel;
	}

	/** Associated kernel. If null then compiler will look for pin on owning node. */
	UPCGComputeKernel* Kernel = nullptr;

	/** Pin label. */
	FName Label;
};

PCG_API uint32 GetTypeHash(const FPCGPinReference& In);

/** A connection for wiring kernels within a node. */
struct FPCGKernelEdge
{
	FPCGKernelEdge(const FPCGPinReference& InUpstreamPin, const FPCGPinReference& InDownstreamPin)
		: UpstreamPin(InUpstreamPin)
		, DownstreamPin(InDownstreamPin)
	{
	}

	bool IsConnectedToNodeInput() const { return UpstreamPin.Kernel == nullptr; }
	bool IsConnectedToNodeOutput() const { return DownstreamPin.Kernel == nullptr; }

	UPCGComputeKernel* GetUpstreamKernel() const { return UpstreamPin.Kernel; }
	UPCGComputeKernel* GetDownstreamKernel() const { return DownstreamPin.Kernel; }

	FPCGPinReference UpstreamPin;
	FPCGPinReference DownstreamPin;
};

/** An input or output pin of a kernel. Compute graph does not internally have 'pins' so this is useful for mapping between kernel data and PCG pins. */
USTRUCT()
struct FPCGKernelPin
{
	GENERATED_BODY()

public:
	FPCGKernelPin() = default;

	explicit FPCGKernelPin(int32 InKernelIndex, FName InPinLabel, bool bInIsInput)
		: KernelIndex(InKernelIndex)
		, PinLabel(InPinLabel)
		, bIsInput(bInIsInput)
	{
	}

	PCG_API bool operator==(const FPCGKernelPin& Other) const = default;

public:
	UPROPERTY()
	int32 KernelIndex = INDEX_NONE;

	UPROPERTY()
	FName PinLabel = NAME_None;

	UPROPERTY()
	bool bIsInput = false;

	PCG_API friend uint32 GetTypeHash(const FPCGKernelPin& In);
};

/** Helper struct for serializing data labels. */
USTRUCT()
struct FPCGDataLabels
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FString> Labels;
};

/** Helper struct for serializing map of pin name to data labels. */
USTRUCT()
struct FPCGPinDataLabels
{
	GENERATED_BODY()

	UPROPERTY()
	TMap</*PinLabel*/FName, FPCGDataLabels> PinToDataLabels;
};
