// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGTransformPointsDataInterface.h"

#include "PCGContext.h"
#include "Compute/PCGComputeGraph.h"
#include "Compute/PCGDataBinding.h"
#include "Elements/PCGTransformPoints.h"
#include "Compute/Elements/PCGTransformPointsKernel.h"

#include "GlobalRenderResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RHIResources.h"
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "ShaderParameterMetadataBuilder.h"
#include "Algo/Find.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGTransformPointsDataInterface)

#define LOCTEXT_NAMESPACE "PCGTransformPointsDataInterface"

void UPCGTransformPointsDataInterface::GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const
{
	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetOffsetMin"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetOffsetMax"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetAbsoluteOffset"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetRotationMin"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetRotationMax"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetAbsoluteRotation"))
	.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetScaleMin"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetScaleMax"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Float, 3));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetAbsoluteScale"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetUniformScale"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetRecomputeSeed"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetApplyToAttribute"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Uint, 1));

	OutFunctions.AddDefaulted_GetRef()
		.SetName(TEXT("TransformPoints_GetAttributeId"))
		.AddReturnType(FShaderValueType::Get(EShaderFundamentalType::Int, 1));
}

BEGIN_SHADER_PARAMETER_STRUCT(FPCGTransformPointsDataInterfaceParameters,)
	SHADER_PARAMETER(FVector3f, OffsetMin)
	SHADER_PARAMETER(FVector3f, OffsetMax)
	SHADER_PARAMETER(uint32, bAbsoluteOffset)
	SHADER_PARAMETER(FVector3f, RotationMin)
	SHADER_PARAMETER(FVector3f, RotationMax)
	SHADER_PARAMETER(uint32, bAbsoluteRotation)
	SHADER_PARAMETER(FVector3f, ScaleMin)
	SHADER_PARAMETER(FVector3f, ScaleMax)
	SHADER_PARAMETER(uint32, bAbsoluteScale)
	SHADER_PARAMETER(uint32, bUniformScale)
	SHADER_PARAMETER(uint32, bRecomputeSeed)
	SHADER_PARAMETER(uint32, bApplyToAttribute)
	SHADER_PARAMETER(int32, AttributeId)
END_SHADER_PARAMETER_STRUCT()

void UPCGTransformPointsDataInterface::GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const
{
	InOutBuilder.AddNestedStruct<FPCGTransformPointsDataInterfaceParameters>(UID);
}

void UPCGTransformPointsDataInterface::GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const
{
	TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("DataInterfaceName"), InDataInterfaceName },
	};

	OutHLSL += FString::Format(TEXT(
		"float3 {DataInterfaceName}_OffsetMin;\n"
		"float3 {DataInterfaceName}_OffsetMax;\n"
		"uint {DataInterfaceName}_bAbsoluteOffset;\n"
		"float3 {DataInterfaceName}_RotationMin;\n"
		"float3 {DataInterfaceName}_RotationMax;\n"
		"uint {DataInterfaceName}_bAbsoluteRotation;\n"
		"float3 {DataInterfaceName}_ScaleMin;\n"
		"float3 {DataInterfaceName}_ScaleMax;\n"
		"uint {DataInterfaceName}_bAbsoluteScale;\n"
		"uint {DataInterfaceName}_bUniformScale;\n"
		"uint {DataInterfaceName}_bRecomputeSeed;\n"
		"uint {DataInterfaceName}_bApplyToAttribute;\n"
		"int {DataInterfaceName}_AttributeId;\n"
		"\n"
		"float3 TransformPoints_GetOffsetMin_{DataInterfaceName}() { return {DataInterfaceName}_OffsetMin; }\n"
		"float3 TransformPoints_GetOffsetMax_{DataInterfaceName}() { return {DataInterfaceName}_OffsetMax; }\n"
		"uint TransformPoints_GetAbsoluteOffset_{DataInterfaceName}() { return {DataInterfaceName}_bAbsoluteOffset; }\n"
		"float3 TransformPoints_GetRotationMin_{DataInterfaceName}() { return {DataInterfaceName}_RotationMin; }\n"
		"float3 TransformPoints_GetRotationMax_{DataInterfaceName}() { return {DataInterfaceName}_RotationMax; }\n"
		"uint TransformPoints_GetAbsoluteRotation_{DataInterfaceName}() { return {DataInterfaceName}_bAbsoluteRotation; }\n"
		"float3 TransformPoints_GetScaleMin_{DataInterfaceName}() { return {DataInterfaceName}_ScaleMin; }\n"
		"float3 TransformPoints_GetScaleMax_{DataInterfaceName}() { return {DataInterfaceName}_ScaleMax; }\n"
		"uint TransformPoints_GetAbsoluteScale_{DataInterfaceName}() { return {DataInterfaceName}_bAbsoluteScale; }\n"
		"uint TransformPoints_GetUniformScale_{DataInterfaceName}() { return {DataInterfaceName}_bUniformScale; }\n"
		"uint TransformPoints_GetRecomputeSeed_{DataInterfaceName}() { return {DataInterfaceName}_bRecomputeSeed; }\n"
		"uint TransformPoints_GetApplyToAttribute_{DataInterfaceName}() { return {DataInterfaceName}_bApplyToAttribute; }\n"
		"int TransformPoints_GetAttributeId_{DataInterfaceName}() { return {DataInterfaceName}_AttributeId; }\n"
		), TemplateArgs);
}

UComputeDataProvider* UPCGTransformPointsDataInterface::CreateDataProvider() const
{
	return NewObject<UPCGTransformPointsDataProvider>();
}

FComputeDataProviderRenderProxy* UPCGTransformPointsDataProvider::GetRenderProxy()
{
	const UPCGTransformPointsSettings* Settings = CastChecked<UPCGTransformPointsSettings>(GetProducerKernel()->GetSettings());

	FPCGTransformPointsDataProviderProxy::FData ProxyData;

	ProxyData.OffsetMin = Settings->OffsetMin;
	ProxyData.OffsetMax = Settings->OffsetMax;
	ProxyData.bAbsoluteOffset = Settings->bAbsoluteOffset;
	ProxyData.RotationMin = Settings->RotationMin.Euler();
	ProxyData.RotationMax = Settings->RotationMax.Euler();
	ProxyData.bAbsoluteRotation = Settings->bAbsoluteRotation;
	ProxyData.ScaleMin = Settings->ScaleMin;
	ProxyData.ScaleMax = Settings->ScaleMax;
	ProxyData.bAbsoluteScale = Settings->bAbsoluteScale;
	ProxyData.bUniformScale = Settings->bUniformScale;
	ProxyData.bRecomputeSeed = Settings->bRecomputeSeed;
	ProxyData.bApplyToAttribute = Settings->bApplyToAttribute;
	ProxyData.AttributeId = AttributeId;

	return new FPCGTransformPointsDataProviderProxy(MoveTemp(ProxyData));
}

void UPCGTransformPointsDataProvider::Reset()
{
	Super::Reset();

	AttributeId = INDEX_NONE;
}

bool UPCGTransformPointsDataProvider::PrepareForExecute_GameThread(UPCGDataBinding* InBinding)
{
	if (!Super::PrepareForExecute_GameThread(InBinding))
	{
		return false;
	}

	TSharedPtr<FPCGContextHandle> ContextHandle = InBinding->GetContextHandle().Pin();
	FPCGContext* Context = ContextHandle ? ContextHandle->GetContext() : nullptr;
	if (!ensure(Context))
	{
		return true;
	}

	const UPCGTransformPointsSettings* Settings = CastChecked<UPCGTransformPointsSettings>(GetProducerKernel()->GetSettings());

	if (Settings->bApplyToAttribute && AttributeId == INDEX_NONE)
	{
		const TSharedPtr<const FPCGDataCollectionDesc> InputDataDesc = InBinding->GetCachedKernelPinDataDesc(GetProducerKernel(), PCGPinConstants::DefaultInputLabel, /*bIsInput=*/true);

		if (!ensure(InputDataDesc))
		{
			return true;
		}

		bool bAnyPointsPresent = false;

		for (const FPCGDataDesc& Desc : InputDataDesc->GetDataDescriptions())
		{
			if (Desc.GetElementCount().X <= 0)
			{
				continue;
			}

			bAnyPointsPresent = true;
			
			const FPCGKernelAttributeDesc* It = Algo::FindByPredicate(Desc.GetAttributeDescriptions(), [Settings](const FPCGKernelAttributeDesc& AttributeDesc)
			{
				return AttributeDesc.GetAttributeKey().GetIdentifier().Name == Settings->AttributeName && AttributeDesc.GetAttributeKey().GetType() == EPCGKernelAttributeType::Transform;
			});
			
			if (It)
			{
				AttributeId = It->GetAttributeId();
				break;
			}
		}

		// Mute this error if the point data is empty.
		if (AttributeId == INDEX_NONE && !InputDataDesc->GetDataDescriptions().IsEmpty() && bAnyPointsPresent)
		{
			PCG_KERNEL_VALIDATION_ERR(Context, Settings, FText::Format(
				LOCTEXT("TransformPointsAttributeNotFound", "Transfrom points attribute '{0}' not found."),
				FText::FromName(Settings->AttributeName)));
		}
	}

	return true;
}

bool FPCGTransformPointsDataProviderProxy::IsValid(FValidationData const& InValidationData) const
{
	return InValidationData.ParameterStructSize == sizeof(FParameters);
}

void FPCGTransformPointsDataProviderProxy::GatherDispatchData(FDispatchData const& InDispatchData)
{
	const TStridedView<FParameters> ParameterArray = MakeStridedParameterView<FParameters>(InDispatchData);

	for (int32 InvocationIndex = 0; InvocationIndex < ParameterArray.Num(); ++InvocationIndex)
	{
		FParameters& Parameters = ParameterArray[InvocationIndex];
		Parameters.OffsetMin = static_cast<FVector3f>(Data.OffsetMin);
		Parameters.OffsetMax = static_cast<FVector3f>(Data.OffsetMax);
		Parameters.bAbsoluteOffset = Data.bAbsoluteOffset;
		Parameters.RotationMin = static_cast<FVector3f>(Data.RotationMin);
		Parameters.RotationMax = static_cast<FVector3f>(Data.RotationMax);
		Parameters.bAbsoluteRotation = Data.bAbsoluteRotation;
		Parameters.ScaleMin = static_cast<FVector3f>(Data.ScaleMin);
		Parameters.ScaleMax = static_cast<FVector3f>(Data.ScaleMax);
		Parameters.bAbsoluteScale = Data.bAbsoluteScale;
		Parameters.bUniformScale = Data.bUniformScale;
		Parameters.bRecomputeSeed = Data.bRecomputeSeed;
		Parameters.bApplyToAttribute = Data.bApplyToAttribute;
		Parameters.AttributeId = Data.AttributeId;
	}
}

#undef LOCTEXT_NAMESPACE
