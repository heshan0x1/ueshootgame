// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGExtractAttribute.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "Utils/PCGLogErrors.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGExtractAttribute)

#define LOCTEXT_NAMESPACE "PCGExtractAttributeElement"

void PCGExtractAttribute::ExtractAttribute(const PCGExtractAttribute::FExtractAttributeParams& Params)
{
	check(Params.Context && Params.InputSource && Params.OutputAttributeName)
	TArray<FPCGTaggedData> Inputs = Params.Context->InputData.GetInputsByPin(Params.InputLabel);

	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		const FPCGTaggedData& Input = Inputs[InputIndex];

		if (!Input.Data || (Params.OptionalClassRequirement.IsSet() && *Params.OptionalClassRequirement && !Input.Data->IsA(*Params.OptionalClassRequirement)))
		{
			PCGLog::InputOutput::LogInvalidInputDataError(Params.Context);
			continue;
		}

		TArray<FPCGTaggedData>& Outputs = Params.Context->OutputData.TaggedData;

		const FPCGAttributePropertyInputSelector InputSource = Params.InputSource->CopyAndFixLast(Input.Data);
		const FPCGAttributePropertyOutputSelector OutputTarget = Params.OutputAttributeName->CopyAndFixSource(&InputSource);

		TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Input.Data, InputSource);
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Input.Data, InputSource);

		if (!Accessor || !Keys)
		{
			PCGLog::Metadata::LogFailToCreateAccessorError(InputSource, Params.Context);
			continue;
		}

		const int32 Index = Params.Index;

		if (Index < 0 || Index >= Keys->GetNum())
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("IndexOutOfBounds", "Index for input {0} is out of bounds. Index: {1}; Number of Elements: {2}"), InputIndex, Index, Keys->GetNum()), Params.Context);
			continue;
		}

		const FName OutputAttributeName = OutputTarget.GetName();

		UPCGParamData* OutputParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Params.Context);

		auto ExtractAttribute = [&Params, OutputAttributeName, &OutputParamData, &Accessor, &Index, &Keys]<typename AttributeType>(AttributeType) -> bool
		{
			AttributeType Value{};

			// Should never fail, as OutputType == Accessor->UnderlyingType
			if (!ensure(Accessor->Get<AttributeType>(Value, Index, *Keys)))
			{
				return false;
			}

			FPCGMetadataAttribute<AttributeType>* NewAttribute = static_cast<FPCGMetadataAttribute<AttributeType>*>(
				OutputParamData->Metadata->CreateAttribute<AttributeType>(OutputAttributeName, Value, /*bAllowInterpolation=*/true, /*bOverrideParent=*/false));

			if (!NewAttribute)
			{
				PCGLog::Metadata::LogFailToCreateAttributeError<AttributeType>(OutputAttributeName, Params.Context);
				return false;
			}

			OutputParamData->Metadata->AddEntry();

			return true;
		};

		if (PCGMetadataAttribute::CallbackWithRightType(Accessor->GetUnderlyingType(), ExtractAttribute))
		{
			FPCGTaggedData& Output = Outputs.Add_GetRef(Input);
			Output.Data = OutputParamData;
			Output.Pin = Params.OutputLabel;

			if (Params.OnSuccessExtractionCallback.IsSet())
			{
				Params.OnSuccessExtractionCallback(Input);
			}
		}
	}
}

#if WITH_EDITOR
FName UPCGExtractAttributeSettings::GetDefaultNodeName() const
{
	return FName(TEXT("ExtractAttribute"));
}

FText UPCGExtractAttributeSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Extract Attribute");
}

FText UPCGExtractAttributeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Extract an attribute at a given index into a new attribute set.\n"
		"Support any domain. Index needs to be in range of valid indexes for the given domain.");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGExtractAttributeSettings::CreateElement() const
{
	return MakeShared<FPCGExtractAttributeElement>();
}

TArray<FPCGPinProperties> UPCGExtractAttributeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Any).SetRequiredPin();
	return Properties;
}

TArray<FPCGPinProperties> UPCGExtractAttributeSettings::OutputPinProperties() const
{ 
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);
	return Properties;
}

bool FPCGExtractAttributeElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExtractAttributeElement::Execute);

	check(Context);

	const UPCGExtractAttributeSettings* Settings = Context->GetInputSettings<UPCGExtractAttributeSettings>();
	check(Settings);

	PCGExtractAttribute::FExtractAttributeParams Params =
	{
		.Context = Context,
		.InputSource = &Settings->InputSource,
		.Index = Settings->Index,
		.OutputAttributeName = &Settings->OutputAttributeName,
	};

	PCGExtractAttribute::ExtractAttribute(Params);

	return true;
}

#undef LOCTEXT_NAMESPACE
