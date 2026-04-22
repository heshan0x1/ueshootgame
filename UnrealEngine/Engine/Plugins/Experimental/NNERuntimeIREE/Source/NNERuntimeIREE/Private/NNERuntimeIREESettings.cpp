// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREESettings.h"

UNNERuntimeIREESettings::UNNERuntimeIREESettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
{

}

FName UNNERuntimeIREESettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UNNERuntimeIREESettings::GetSectionText() const
{
	return NSLOCTEXT("NNERuntimeIREEPlugin", "NNERuntimeIREESettingsSection", "NNERuntimeIREE");
}
#endif