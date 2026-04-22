// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORTSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NNERuntimeORTSettings)

UNNERuntimeORTSettings::UNNERuntimeORTSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{

}

FName UNNERuntimeORTSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UNNERuntimeORTSettings::GetSectionText() const
{
	return NSLOCTEXT("NNERuntimeORTPlugin", "NNERuntimeORTSettingsSection", "NNERuntimeORT");
}
#endif
