// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityBlueprint.h"

/////////////////////////////////////////////////////
// UEditorUtilityBlueprint

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditorUtilityBlueprint)

UEditorUtilityBlueprint::UEditorUtilityBlueprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UEditorUtilityBlueprint::SupportedByDefaultBlueprintFactory() const
{
	return false;
}

bool UEditorUtilityBlueprint::AlwaysCompileOnLoad() const
{
	return true;
}
