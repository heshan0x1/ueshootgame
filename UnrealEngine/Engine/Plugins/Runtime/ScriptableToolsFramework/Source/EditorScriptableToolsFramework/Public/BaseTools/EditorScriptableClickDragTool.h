// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/ScriptableClickDragTool.h"
#include "EditorScriptableClickDragTool.generated.h"

/**
 * Editor-Only variant of UScriptableClickDragTool, which gives access to Editor-Only BP functions
 */
UCLASS(MinimalAPI, Transient, Blueprintable)
class UEditorScriptableClickDragTool : public UScriptableClickDragTool
{
	GENERATED_BODY()
public:

};
