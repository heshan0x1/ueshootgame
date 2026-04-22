// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructUtilsEditorUtils.h"

namespace UE::StructUtils
{

namespace Private
{

TOptional<FFindUserFunctionResult> FindUserFunction(const TSharedPtr<IPropertyHandle>& InStructProperty, FName InFuncMetadataName)
{
	FProperty* MetadataProperty = InStructProperty->GetMetaDataProperty();

	FFindUserFunctionResult Result{};

	if (!MetadataProperty || !MetadataProperty->HasMetaData(InFuncMetadataName))
	{
		return {};
	}

	FString FunctionName = MetadataProperty->GetMetaData(InFuncMetadataName);
	if (FunctionName.IsEmpty())
	{
		return {};
	}

	TArray<UObject*> OutObjects;
	InStructProperty->GetOuterObjects(OutObjects);

	// Check for external function references, taken from GetOptions
	if (FunctionName.Contains(TEXT(".")))
	{
		Result.Function = FindObject<UFunction>(nullptr, *FunctionName, EFindObjectFlags::ExactClass);

		if (ensureMsgf(Result.Function && Result.Function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Static), TEXT("[%s] Didn't find function %s or expected it to be static"), *InFuncMetadataName.ToString(), *FunctionName))
		{
			UObject* GetOptionsCDO = Result.Function->GetOuterUClass()->GetDefaultObject();
			Result.Target = GetOptionsCDO;
		}
		else
		{
			return {};
		}
	}
	else if (OutObjects.Num() > 0)
	{
		Result.Target = OutObjects[0];
		Result.Function = Result.Target->GetClass() ? Result.Target->GetClass()->FindFunctionByName(*FunctionName) : nullptr;
	}

	// Only support native functions
	if (!ensureMsgf(Result.Function && Result.Function->IsNative(), TEXT("[%s] Didn't find function %s or expected it to be native"), *InFuncMetadataName.ToString(), *FunctionName))
	{
		return {};
	}

	return (Result.Target != nullptr && Result.Function != nullptr) ? Result : TOptional<FFindUserFunctionResult>{};
}
} // UE::StructUtils::Private

} // UE::StructUtils
