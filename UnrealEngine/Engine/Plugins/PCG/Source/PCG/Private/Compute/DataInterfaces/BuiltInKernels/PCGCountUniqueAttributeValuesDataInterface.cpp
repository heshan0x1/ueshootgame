// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGCountUniqueAttributeValuesDataInterface.h"

#include "Compute/PCGComputeCommon.h"
#include "Compute/PCGDataBinding.h"
#include "Compute/PCGDataDescription.h"
#include "Compute/BuiltInKernels/PCGCountUniqueAttributeValuesKernel.h"
#include "Elements/Metadata/PCGMetadataPartition.h"
#include "Elements/Metadata/PCGMetadataPartitionKernel.h"

#include "GlobalRenderResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RHIResources.h"
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCountUniqueAttributeValuesDataInterface)

void UPCGCountUniqueAttributeValuesDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("CountUniqueValues_GetAttributeToCountId"))
		.AddReturnType(EShaderFundamentalType::Int);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("CountUniqueValues_GetOutputCountAttributeId"))
		.AddReturnType(EShaderFundamentalType::Int);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("CountUniqueValues_GetOutputValueAttributeId"))
		.AddReturnType(EShaderFundamentalType::Int);

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("CountUniqueValues_GetEmitPerDataCounts"))
		.AddReturnType(EShaderFundamentalType::Bool);
}

BEGIN_SHADER_PARAMETER_STRUCT(FPCGCountUniqueAttributeValuesDataInterfaceParameters,)
	SHADER_PARAMETER(int32, AttributeToCountId)
	SHADER_PARAMETER(int32, OutputValueAttributeId)
	SHADER_PARAMETER(int32, OutputCountAttributeId)
	SHADER_PARAMETER(uint32, EmitPerDataCounts)
END_SHADER_PARAMETER_STRUCT()

void UPCGCountUniqueAttributeValuesDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FPCGCountUniqueAttributeValuesDataInterfaceParameters>(UID);
}

void UPCGCountUniqueAttributeValuesDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	OutHLSL += FString::Format(TEXT(
		"int {DataInterfaceName}_AttributeToCountId;\n"
		"int {DataInterfaceName}_OutputValueAttributeId;\n"
		"int {DataInterfaceName}_OutputCountAttributeId;\n"
		"uint {DataInterfaceName}_EmitPerDataCounts;\n"
		"\n"
		"int CountUniqueValues_GetAttributeToCountId_{DataInterfaceName}()\n"
		"{\n"
		"	return {DataInterfaceName}_AttributeToCountId;\n"
		"}\n"
		"\n"
		"int CountUniqueValues_GetOutputValueAttributeId_{DataInterfaceName}()\n"
		"{\n"
		"	return {DataInterfaceName}_OutputValueAttributeId;\n"
		"}\n"
		"\n"
		"int CountUniqueValues_GetOutputCountAttributeId_{DataInterfaceName}()\n"
		"{\n"
		"	return {DataInterfaceName}_OutputCountAttributeId;\n"
		"}\n"
		"\n"
		"bool CountUniqueValues_GetEmitPerDataCounts_{DataInterfaceName}()\n"
		"{\n"
		"	return {DataInterfaceName}_EmitPerDataCounts > 0;\n"
		"}\n"
		), TemplateArgs);
}

UComputeDataProvider* UPCGCountUniqueAttributeValuesDataInterface::CreateDataProvider() const
{
	return NewObject<UPCGCountUniqueAttributeValuesDataProvider>();
}

void UPCGCountUniqueAttributeValuesDataProvider::Initialize(const UComputeDataInterface* InDataInterface, UObject* InBinding, uint64 InInputMask, uint64 InOutputMask)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGCountUniqueAttributeValuesDataProvider::Initialize);

	Super::Initialize(InDataInterface, InBinding, InInputMask, InOutputMask);

	const UPCGCountUniqueAttributeValuesDataInterface* DataInterface = CastChecked<UPCGCountUniqueAttributeValuesDataInterface>(InDataInterface);

	AttributeToCountName = DataInterface->GetAttributeToCountName();
	bEmitPerDataCounts = DataInterface->GetEmitPerDataCounts();
	bOutputRawBuffer = DataInterface->GetOutputRawBuffer();
}

bool UPCGCountUniqueAttributeValuesDataProvider::PrepareForExecute_GameThread(UPCGDataBinding* InBinding)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGCountUniqueAttributeValuesDataInterface::PrepareForExecute_GameThread);
	check(InBinding);

	if (!Super::PrepareForExecute_GameThread(InBinding))
	{
		return false;
	}

	if (!bOutputRawBuffer)
	{
		OutputValueAttributeId = InBinding->GetAttributeId(PCGCountUniqueAttributeValuesConstants::ValueAttributeName, EPCGKernelAttributeType::Int);
		check(OutputValueAttributeId != -1);
		OutputCountAttributeId = InBinding->GetAttributeId(PCGCountUniqueAttributeValuesConstants::ValueCountAttributeName, EPCGKernelAttributeType::Int);
		check(OutputCountAttributeId != -1);
	}

	const TSharedPtr<const FPCGDataCollectionDesc> InputDataDesc = InBinding->GetCachedKernelPinDataDesc(GetProducerKernel(), PCGPinConstants::DefaultInputLabel, /*bIsInput*/true);

	if (!ensure(InputDataDesc))
	{
		return true;
	}

	AttributeToCountId = InBinding->GetAttributeId(AttributeToCountName, EPCGKernelAttributeType::StringKey);

	TArray<TArray<int32>> UniqueStringKeyValuesPerOutputData;

	if (AttributeToCountId != INDEX_NONE)
	{
		if (bEmitPerDataCounts)
		{
			// Gather unique string key values for each data.
			for (const FPCGDataDesc& DataDesc : InputDataDesc->GetDataDescriptions())
			{
				TArray<int32> UniqueStringKeyValues;

				for (const FPCGKernelAttributeDesc& AttributeDesc : DataDesc.GetAttributeDescriptions())
				{
					if (AttributeDesc.GetAttributeId() == AttributeToCountId)
					{
						UniqueStringKeyValues = AttributeDesc.GetUniqueStringKeys();
						break;
					}
				}

				UniqueStringKeyValuesPerOutputData.Emplace_GetRef() = MoveTemp(UniqueStringKeyValues);
			}
		}
		else
		{
			// Gather unique string key values across all data.
			InputDataDesc->GetUniqueStringKeyValues(AttributeToCountId, UniqueStringKeyValuesPerOutputData.Emplace_GetRef());
		}
	}

	return true;
}

FComputeDataProviderRenderProxy* UPCGCountUniqueAttributeValuesDataProvider::GetRenderProxy()
{
	FPCGCountUniqueAttributeValuesProviderProxy::FCountUniqueValuesData_RenderThread Data;
	Data.AttributeToCountId = AttributeToCountId;
	Data.OutputValueAttributeId = OutputValueAttributeId;
	Data.OutputCountAttributeId = OutputCountAttributeId;
	Data.bEmitPerDataCounts = bEmitPerDataCounts;

	return new FPCGCountUniqueAttributeValuesProviderProxy(MoveTemp(Data));
}

void UPCGCountUniqueAttributeValuesDataProvider::Reset()
{
	AttributeToCountName = NAME_None;
	AttributeToCountId = INDEX_NONE;
	OutputValueAttributeId = INDEX_NONE;
	OutputCountAttributeId = INDEX_NONE;
	bEmitPerDataCounts = true;
	bOutputRawBuffer = false;

	Super::Reset();
}

bool FPCGCountUniqueAttributeValuesProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	return (InValidationData.ParameterStructSize == sizeof(FParameters));
}

void FPCGCountUniqueAttributeValuesProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	const TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);
	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		FParameters& Parameters = ParameterArray[InvocationIndex];

		Parameters.AttributeToCountId = Data.AttributeToCountId;
		Parameters.OutputValueAttributeId = Data.OutputValueAttributeId;
		Parameters.OutputCountAttributeId = Data.OutputCountAttributeId;
		Parameters.EmitPerDataCounts = Data.bEmitPerDataCounts ? 1u : 0u;
	}
}
