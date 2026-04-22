// Copyright Epic Games, Inc. All Rights Reserved.

#include "Bindings/MVVMConversionLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MVVMConversionLibrary)

ESlateVisibility UMVVMConversionLibrary::Conv_BoolToSlateVisibility(bool bIsVisible, ESlateVisibility TrueCaseVisibility, ESlateVisibility FalseCaseVisibility)
{
	return bIsVisible ? TrueCaseVisibility : FalseCaseVisibility;
}
