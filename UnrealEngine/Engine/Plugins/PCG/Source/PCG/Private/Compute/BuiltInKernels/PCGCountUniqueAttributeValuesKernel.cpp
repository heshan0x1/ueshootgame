// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/BuiltInKernels/PCGCountUniqueAttributeValuesKernel.h"

#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGParamData.h"
#include "Compute/Elements/PCGComputeGraphElement.h"
#include "Compute/PCGComputeGraph.h"
#include "Compute/PCGDataBinding.h"
#include "Compute/PCGPinPropertiesGPU.h"
#include "Compute/Data/PCGRawBufferData.h"
#include "Compute/DataInterfaces/BuiltInKernels/PCGCountUniqueAttributeValuesDataInterface.h"
#include "Graph/PCGGPUGraphCompilationContext.h"

#include "ShaderCompilerCore.h"
#include "Algo/MaxElement.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCountUniqueAttributeValuesKernel)

#define LOCTEXT_NAMESPACE "PCGCountUniqueAttributeValuesKernel"

bool UPCGCountUniqueAttributeValuesKernel::IsKernelDataValid(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGCountUniqueAttributeValuesKernel::IsKernelDataValid);

	if (!Super::IsKernelDataValid(InContext))
	{
		return false;
	}

	if (!InContext || !InContext->IsComputeContext())
	{
		return true;
	}

	const FPCGComputeGraphContext* Context = static_cast<FPCGComputeGraphContext*>(InContext);
	const UPCGDataBinding* DataBinding = Context->DataBinding.Get();
	if (DataBinding)
	{
		const TSharedPtr<const FPCGDataCollectionDesc> InputDataDesc = DataBinding->GetCachedKernelPinDataDesc(this, PCGPinConstants::DefaultInputLabel, /*bIsInputPin=*/true);

		if (!ensure(InputDataDesc))
		{
			return false;
		}

		if (!InputDataDesc->GetDataDescriptions().IsEmpty())
		{
			FPCGKernelAttributeDesc AttributeDesc;
			bool bConflictingTypesFound = false;
			bool bPresentOnAllData = false;
			InputDataDesc->GetAttributeDesc(AttributeName, AttributeDesc, bConflictingTypesFound, bPresentOnAllData);

			if (!bPresentOnAllData)
			{
				if (!InputDataDesc->GetDataDescriptions().IsEmpty())
				{
					PCG_KERNEL_VALIDATION_ERR(InContext, GetSettings(), FText::Format(
						LOCTEXT("AttributeMissing", "Count attribute '{0}' not found, this attribute must be present on all input data, and be of type String Key."),
						FText::FromName(AttributeName)));
				}

				return false;
			}

			if (bConflictingTypesFound)
			{
				PCG_KERNEL_VALIDATION_ERR(InContext, GetSettings(), FText::Format(
					LOCTEXT("AttributeHasMultipleTypes", "Count attribute '{0}' found with multiple types in input data, all attributes must be of type String Key."),
					FText::FromName(AttributeName)));

				return false;
			}

			if (AttributeDesc.GetAttributeKey().GetType() != EPCGKernelAttributeType::StringKey)
			{
				// Attribute value counting only currently supported for attributes of type StringKey.
				PCG_KERNEL_VALIDATION_ERR(Context, GetSettings(), FText::Format(
					LOCTEXT("AttributeTypeInvalid", "Cannot count values for attribute '{0}', only attributes of type String Key are currently supported."),
					FText::FromName(AttributeName)));

				return false;
			}
		}
	}

	return true;
}

TSharedPtr<const FPCGDataCollectionDesc> UPCGCountUniqueAttributeValuesKernel::ComputeOutputBindingDataDesc(FName InOutputPinLabel, UPCGDataBinding* InBinding) const
{
	check(InBinding);

	// Code assumes single output pin.
	if (!ensure(InOutputPinLabel == PCGPinConstants::DefaultOutputLabel))
	{
		return nullptr;
	}

	const FPCGKernelPin InputKernelPin(GetKernelIndex(), PCGPinConstants::DefaultInputLabel, /*bIsInput=*/true);
	const TSharedPtr<const FPCGDataCollectionDesc> InputDesc = InBinding->ComputeKernelPinDataDesc(InputKernelPin);

	if (!ensure(InputDesc))
	{
		return nullptr;
	}

	TSharedPtr<FPCGDataCollectionDesc> OutDataDesc = FPCGDataCollectionDesc::MakeShared();
	TArray<FPCGDataDesc>& OutDataDescs = OutDataDesc->GetDataDescriptionsMutable();

	const int32 AttributeId = InBinding->GetAttributeId(AttributeName, EPCGKernelAttributeType::StringKey);

	if (AttributeId != INDEX_NONE)
	{
		// Compute a set of (value, count) pairs across all data.
		TArray<int32> UniqueStringKeyValues;
		InputDesc->GetUniqueStringKeyValues(AttributeId, UniqueStringKeyValues);

		// If the highest string key value is 3, we'll allocate 4 counters. String key value 0 is reserved for empty/null string.
		const int32* MaxStringKeyValue = Algo::MaxElement(UniqueStringKeyValues);
		const int32 ElementCount = (MaxStringKeyValue ? *MaxStringKeyValue : 0) + 1;

		// Base data description object, copied N times below to populate the final description.
		FPCGDataDesc CountDataDesc;
		
		if (bOutputRawBuffer)
		{
			CountDataDesc = FPCGDataDesc(FPCGDataTypeInfoRawBuffer::AsId(), ElementCount * 2);
		}
		else
		{
			CountDataDesc = FPCGDataDesc(EPCGDataType::Param, ElementCount);
			CountDataDesc.AddAttribute(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueAttributeName, EPCGKernelAttributeType::Int), InBinding);
			CountDataDesc.AddAttribute(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueCountAttributeName, EPCGKernelAttributeType::Int), InBinding);
		}

		// Raw buffer output not supported for multiple data currently.
		ensure(!bEmitPerDataCounts || !bOutputRawBuffer);

		if (bEmitPerDataCounts)
		{
			const TConstArrayView<FPCGDataDesc> InputDataDescs = InputDesc->GetDataDescriptions();

			// Output data is array of (value, count) pairs for each data.
			for (int InputIndex = 0; InputIndex < InputDataDescs.Num(); ++InputIndex)
			{
				const FPCGDataDesc& DataDesc = InputDataDescs[InputIndex];

				// Data descriptions have multiple collection objects so worth to move last element for efficiency, otherwise copy.
				if (InputIndex < InputDataDescs.Num() - 1)
				{
					OutDataDescs.Add(CountDataDesc);
				}
				else
				{
					OutDataDescs.Add(MoveTemp(CountDataDesc));
				}
			}
		}
		else
		{
			OutDataDescs.Add(MoveTemp(CountDataDesc));
		}
	}
	else
	{
		// Attribute not present on input data. We can't create a zero sized buffer on the GPU, so create a null output.
		if (bOutputRawBuffer)
		{
			// Null output: One pair (string key 0 (invalid), value count 0).
			OutDataDescs.Emplace(FPCGDataTypeInfoRawBuffer::AsId(), 2);
		}
		else
		{
			// Null output: One entry: string key 0, value count 0.
			FPCGDataDesc& CountDataDesc = OutDataDescs.Emplace_GetRef(EPCGDataType::Param, 1);
			CountDataDesc.AddAttribute(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueAttributeName, EPCGKernelAttributeType::Int), InBinding);
			CountDataDesc.AddAttribute(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueCountAttributeName, EPCGKernelAttributeType::Int), InBinding);
		}
	}

	return OutDataDesc;
}

int UPCGCountUniqueAttributeValuesKernel::ComputeThreadCount(const UPCGDataBinding* InBinding) const
{
	if (const TSharedPtr<const FPCGDataCollectionDesc> InputPinDesc = InBinding->GetCachedKernelPinDataDesc(this, PCGPinConstants::DefaultInputLabel, /*bIsInput=*/true); ensure(InputPinDesc))
	{
		const int ElementCount = InputPinDesc->ComputeTotalElementCount();

		const int32 PartitionAttributeId = InBinding->GetAttributeId(AttributeName, EPCGKernelAttributeType::StringKey);
		const int ValueCount = (PartitionAttributeId != INDEX_NONE) ? InputPinDesc->GetNumStringKeyValues(PartitionAttributeId) : 0;

		// One thread per element, but also make sure we have enough threads to write each unique attribute value (rather than leaving uninitialized).
		return FMath::Max(ElementCount, ValueCount);
	}
	else
	{
		return 0;
	}
}

bool UPCGCountUniqueAttributeValuesKernel::DoesOutputPinRequireZeroInitialization(FName InOutputPinLabel) const
{
	// We are atomically incrementing the values on the output so we need to ensure the values are 0-initialized.
	return InOutputPinLabel == PCGPinConstants::DefaultOutputLabel;
}

#if WITH_EDITOR
FString UPCGCountUniqueAttributeValuesKernel::GetCookedSource(FPCGGPUCompilationContext& InOutContext) const
{
	FString SourceFile;
	ensure(LoadShaderSourceFile(GetSourceFilePath(), EShaderPlatform::SP_PCD3D_SM5, &SourceFile, nullptr));
	
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("RawDataOutputEnabled"), bOutputRawBuffer ? TEXT("1") : TEXT("0") },
	};

	SourceFile = FString::Format(*SourceFile, TemplateArgs);

	return SourceFile;
}

void UPCGCountUniqueAttributeValuesKernel::CreateAdditionalInputDataInterfaces(FPCGGPUCompilationContext& InOutContext, UObject* InObjectOuter, TArray<TObjectPtr<UComputeDataInterface>>& OutDataInterfaces) const
{
	Super::CreateAdditionalInputDataInterfaces(InOutContext, InObjectOuter, OutDataInterfaces);

	TObjectPtr<UPCGCountUniqueAttributeValuesDataInterface> KernelDI = InOutContext.NewObject_AnyThread<UPCGCountUniqueAttributeValuesDataInterface>(InObjectOuter);
	KernelDI->SetProducerKernel(this);
	KernelDI->SetAttributeToCountName(AttributeName);
	KernelDI->SetEmitPerDataCounts(bEmitPerDataCounts);
	KernelDI->SetOutputRawBuffer(bOutputRawBuffer);

	OutDataInterfaces.Add(KernelDI);
}
#endif

void UPCGCountUniqueAttributeValuesKernel::GetKernelAttributeKeys(TArray<FPCGKernelAttributeKey>& OutKeys) const
{
	// Register the attribute this node creates.
	if (!bOutputRawBuffer)
	{
		OutKeys.AddUnique(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueAttributeName, EPCGKernelAttributeType::Int));
		OutKeys.AddUnique(FPCGKernelAttributeKey(PCGCountUniqueAttributeValuesConstants::ValueCountAttributeName, EPCGKernelAttributeType::Int));
	}
}

void UPCGCountUniqueAttributeValuesKernel::GetInputPins(TArray<FPCGPinProperties>& OutPins) const
{
	OutPins.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point);
}

void UPCGCountUniqueAttributeValuesKernel::GetOutputPins(TArray<FPCGPinPropertiesGPU>& OutPins) const
{
	// Attribute set with a value count attribute, element count equal to number of unique values of the counted attribute.
	OutPins.Emplace(PCGPinConstants::DefaultOutputLabel, bOutputRawBuffer ? FPCGDataTypeInfoRawBuffer::AsId() : FPCGDataTypeInfoParam::AsId());
}

#undef LOCTEXT_NAMESPACE
