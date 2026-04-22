// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGCustomHLSLDataInterface.h"

#include "PCGContext.h"
#include "Compute/PCGComputeGraph.h"
#include "Compute/PCGDataBinding.h"
#include "Compute/Elements/PCGCustomHLSL.h"
#include "Compute/Elements/PCGCustomHLSLKernel.h"

#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCustomHLSLDataInterface)

BEGIN_SHADER_PARAMETER_STRUCT(FPCGCustomHLSLDataInterfaceParameters,)
	SHADER_PARAMETER(int32, NumElements)
	//SHADER_PARAMETER(FIntVector2, NumElements2D)
	SHADER_PARAMETER(int32, ThreadCountMultiplier)
	SHADER_PARAMETER(int32, FixedThreadCount)
END_SHADER_PARAMETER_STRUCT()

void UPCGCustomHLSLDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FPCGCustomHLSLDataInterfaceParameters>(UID);
}

UComputeDataProvider* UPCGCustomHLSLDataInterface::CreateDataProvider() const
{
	return NewObject<UPCGCustomHLSLDataProvider>();
}

FComputeDataProviderRenderProxy* UPCGCustomHLSLDataProvider::GetRenderProxy()
{
	FPCGCustomHLSLDataProviderProxy::FCustomHLSLData ProxyData;

	if (ensure(KernelParams))
	{
		ProxyData.NumElements = KernelParams->GetValueInt(GET_MEMBER_NAME_CHECKED(UPCGCustomHLSLSettings, NumElements));
		//ProxyData.NumElements2D = KernelParams->GetValueInt2(GET_MEMBER_NAME_CHECKED(UPCGCustomHLSLSettings, NumElements2D));
		ProxyData.ThreadCountMultiplier = KernelParams->GetValueInt(GET_MEMBER_NAME_CHECKED(UPCGCustomHLSLSettings, ThreadCountMultiplier));
		ProxyData.FixedThreadCount = KernelParams->GetValueInt(GET_MEMBER_NAME_CHECKED(UPCGCustomHLSLSettings, FixedThreadCount));
	}

	return new FPCGCustomHLSLDataProviderProxy(MoveTemp(ProxyData));
}

void UPCGCustomHLSLDataProvider::Reset()
{
	Super::Reset();

	KernelParams = nullptr;
}

bool UPCGCustomHLSLDataProvider::PrepareForExecute_GameThread(UPCGDataBinding* InBinding)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGCustomHLSLDataProvider::PrepareForExecute_GameThread);
	check(InBinding);

	if (!Super::PrepareForExecute_GameThread(InBinding))
	{
		return false;
	}

	KernelParams = InBinding->GetCachedKernelParams(GetProducerKernel());

	return true;
}

bool FPCGCustomHLSLDataProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	return InValidationData.ParameterStructSize == sizeof(FParameters);
}

void FPCGCustomHLSLDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	const TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);

	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		FParameters& Parameters = ParameterArray[InvocationIndex];
		Parameters.NumElements = Data.NumElements;
		//Parameters.NumElements2D = static_cast<FVector2f>(Data.NumElements2D);
		Parameters.ThreadCountMultiplier = Data.ThreadCountMultiplier;
		Parameters.FixedThreadCount = Data.FixedThreadCount;
	}
}
