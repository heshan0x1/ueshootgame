// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeDataInterfaceBuffer.h"

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterMetadataBuilder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComputeDataInterfaceBuffer)

static bool SupportsAtomics(FShaderValueTypeHandle InValueType)
{
	return (InValueType->DimensionType == EShaderFundamentalDimensionType::Scalar && (InValueType->Type == EShaderFundamentalType::Int || InValueType->Type == EShaderFundamentalType::Uint));
}

void UComputeDataInterfaceBuffer::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadNumValues"))
		.AddReturnType(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadValue"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("ReadValueUAV"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint);
}

void UComputeDataInterfaceBuffer::GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WriteValue"))
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(ValueType);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WriteAtomicAdd"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(ValueType);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WriteAtomicMin"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(ValueType);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("WriteAtomicMax"))
		.AddReturnType(ValueType)
		.AddParam(EShaderFundamentalType::Uint)
		.AddParam(ValueType);
}

BEGIN_SHADER_PARAMETER_STRUCT(FBufferDataInterfaceParameters, )
	SHADER_PARAMETER(uint32, BufferElementCount)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, BufferSRV)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, BufferUAV)
END_SHADER_PARAMETER_STRUCT()

void UComputeDataInterfaceBuffer::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FBufferDataInterfaceParameters>(UID);
}

TCHAR const* UComputeDataInterfaceBuffer::TemplateFilePath = TEXT("/Plugin/ComputeFramework/Private/ComputeDataInterfaceBuffer.ush");

TCHAR const* UComputeDataInterfaceBuffer::GetShaderVirtualPath() const
{
	return TemplateFilePath;
}

void UComputeDataInterfaceBuffer::GetShaderHash(FString& InOutKey) const
{
	GetShaderFileHash(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5).AppendString(InOutKey);
}

void UComputeDataInterfaceBuffer::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	const FString ValueTypeName = ValueType->ToString();
	const int32 ValueTypeStride = ValueType->GetResourceElementSize();
	const bool bSupportAtomic = SupportsAtomics(ValueType);

	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
		{ TEXT("ValueType"), ValueTypeName },
		{ TEXT("ValueTypeStride"), ValueTypeStride },
		{ TEXT("SupportAtomic"), bSupportAtomic ? 1 : 0 },
		{ TEXT("SplitReadWrite"), bAllowReadWrite ? 0 : 1 },
	};

	FString TemplateFile;
	LoadShaderSourceFile(TemplateFilePath, EShaderPlatform::SP_PCD3D_SM5, &TemplateFile, nullptr);
	OutHLSL += FString::Format(*TemplateFile, TemplateArgs);
}

UComputeDataProvider* UComputeDataInterfaceBuffer::CreateDataProvider() const
{
	UBufferDataProvider* Provider = NewObject<UBufferDataProvider>();
	Provider->ElementCount = ElementCount;
	Provider->ElementStride = ValueType->GetResourceElementSize();
	Provider->bClearBeforeUse = bClearBeforeUse;
	return Provider;
}

FComputeDataProviderRenderProxy* UBufferDataProvider::GetRenderProxy()
{
	return new FBufferDataProviderProxy(ElementCount, ElementStride, bClearBeforeUse);
}

FBufferDataProviderProxy::FBufferDataProviderProxy(int32 InElementCount, int32 InElementStride, bool bInClearBeforeUse)
	: ElementCount(InElementCount)
	, ElementStride(InElementStride)
	, bClearBeforeUse(bInClearBeforeUse)
{
}

bool FBufferDataProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	if (InValidationData.ParameterStructSize != sizeof(FParameters))
	{
		return false;
	}
	
	if (ElementCount <= 0)
	{
		return false;
	}

	return true;
}

void FBufferDataProviderProxy::AllocateResources(FRDGBuilder& GraphBuilder, FAllocationData const& InAllocationData)
{
	// If we are using a raw type alias for the buffer then we need to adjust stride and count.
	check(ElementStride % 4 == 0);

	Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(ElementStride * ElementCount), TEXT("ComputeFrameworkBuffer"), ERDGBufferFlags::None);
	BufferSRV = GraphBuilder.CreateSRV(Buffer);
	BufferUAV = GraphBuilder.CreateUAV(Buffer, ERDGUnorderedAccessViewFlags::SkipBarrier);

	if (bClearBeforeUse)
	{
		AddClearUAVPass(GraphBuilder, BufferUAV, 0);
	}
}

void FBufferDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);
	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		FParameters& Parameters = ParameterArray[InvocationIndex];
		Parameters.BufferElementCount = ElementCount;
		Parameters.BufferSRV = BufferSRV;
		Parameters.BufferUAV = BufferUAV;
	}
}
