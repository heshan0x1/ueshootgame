// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyBindingBindableStructDescriptor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PropertyBindingBindableStructDescriptor)

FPropertyBindingBindableStructDescriptor::~FPropertyBindingBindableStructDescriptor()
{
}

FString FPropertyBindingBindableStructDescriptor::ToString() const
{
	FStringBuilderBase Result;

	Result += TEXT("'");
	Result += Name.ToString();
	Result += TEXT("'");

	return Result.ToString();
}
