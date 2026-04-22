// Copyright Epic Games, Inc. All Rights Reserved.

#include "Param/RigVMDispatch_SetLayerParameter.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMCore/RigVM.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMDispatch_SetLayerParameter)

const FName FRigVMDispatch_SetLayerParameter::ExecuteContextName = TEXT("ExecuteContext");
const FName FRigVMDispatch_SetLayerParameter::ValueName = TEXT("Value");
const FName FRigVMDispatch_SetLayerParameter::TypeHandleName = TEXT("Type");
const FName FRigVMDispatch_SetLayerParameter::ParameterName = TEXT("Parameter");
const FName FRigVMDispatch_SetLayerParameter::ParameterIdName = TEXT("ParameterId");

FRigVMDispatch_SetLayerParameter::FRigVMDispatch_SetLayerParameter()
{
	FactoryScriptStruct = StaticStruct();
}

FName FRigVMDispatch_SetLayerParameter::GetArgumentNameForOperandIndex(int32 InOperandIndex, int32 InTotalOperands, FRigVMRegistryHandle& InRegistry) const
{
	static const FName ArgumentNames[] = 
	{
		ParameterName,
		ValueName,
		ParameterIdName,
		TypeHandleName
	};
	check(InTotalOperands == UE_ARRAY_COUNT(ArgumentNames));
	return ArgumentNames[InOperandIndex];
}

#if WITH_EDITOR
FString FRigVMDispatch_SetLayerParameter::GetArgumentMetaData(const FName& InArgumentName, const FName& InMetaDataKey) const
{
	if ((InArgumentName == TypeHandleName || InArgumentName == ParameterIdName) &&
		InMetaDataKey == FRigVMStruct::SingletonMetaName)
	{
		return TEXT("True");
	}
	else if(InArgumentName == ParameterName && InMetaDataKey == FRigVMStruct::CustomWidgetMetaName)
	{
		return TEXT("ParamName");
	}

	return Super::GetArgumentMetaData(InArgumentName, InMetaDataKey);
}
#endif

const TArray<FRigVMTemplateArgumentInfo>& FRigVMDispatch_SetLayerParameter::GetArgumentInfos(FRigVMRegistryHandle& InRegistry) const
{
	if(CachedArgumentInfos.IsEmpty())
	{
		static const TArray<FRigVMTemplateArgument::ETypeCategory> ValueCategories =
		{
			FRigVMTemplateArgument::ETypeCategory_SingleAnyValue,
			FRigVMTemplateArgument::ETypeCategory_ArrayAnyValue
		};
	
		CachedArgumentInfos.Emplace(ParameterName, ERigVMPinDirection::Input, RigVMTypeUtils::TypeIndex::FName);
		CachedArgumentInfos.Emplace(ValueName, ERigVMPinDirection::Input, ValueCategories);
		CachedArgumentInfos.Emplace(ParameterIdName, ERigVMPinDirection::Hidden, RigVMTypeUtils::TypeIndex::UInt32);
		CachedArgumentInfos.Emplace(TypeHandleName, ERigVMPinDirection::Hidden, RigVMTypeUtils::TypeIndex::UInt32);
	}

	return CachedArgumentInfos;
}

TArray<FRigVMExecuteArgument>& FRigVMDispatch_SetLayerParameter::GetExecuteArguments_Impl(const FRigVMDispatchContext& InContext) const
{
	static TArray<FRigVMExecuteArgument> Arguments = { { ExecuteContextName, ERigVMPinDirection::IO } };
	return Arguments;
}

FRigVMTemplateTypeMap FRigVMDispatch_SetLayerParameter::OnNewArgumentType(const FName& InArgumentName, TRigVMTypeIndex InTypeIndex, FRigVMRegistryHandle& InRegistry) const
{
	FRigVMTemplateTypeMap Types;
	Types.Add(ParameterName, RigVMTypeUtils::TypeIndex::FName);
	Types.Add(ValueName, InTypeIndex);
	Types.Add(ParameterIdName, RigVMTypeUtils::TypeIndex::UInt32);
	Types.Add(TypeHandleName, RigVMTypeUtils::TypeIndex::UInt32);
	return Types;
}

void FRigVMDispatch_SetLayerParameter::Execute(FRigVMExtendedExecuteContext& InContext, FRigVMMemoryHandleArray Handles, FRigVMPredicateBranchArray RigVMBranches)
{
	// Deprecated stub
}

