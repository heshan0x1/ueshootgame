// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyHandle.h"
#include "Templates/SharedPointer.h"

class UFunction;
class UObject;

namespace UE::StructUtils
{
namespace Private
{
	
struct FFindUserFunctionResult
{
	UFunction* Function = nullptr;
	UObject* Target = nullptr;
};
	
TOptional<FFindUserFunctionResult> FindUserFunction(const TSharedPtr<IPropertyHandle>& InStructProperty, FName InFuncMetadataName);

/** @return true property handle holds struct property of type T.  */
template<typename T> requires TModels_V<CStaticStructProvider, T>
bool IsScriptStruct(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	if (!PropertyHandle)
	{
		return false;
	}

	const FStructProperty* StructProperty = CastField<const FStructProperty>(PropertyHandle->GetProperty());
	return StructProperty && StructProperty->Struct->IsA(TBaseStructure<T>::Get()->GetClass());
}
	
} // UE::StructUtils::Private
} // UE::StructUtils
