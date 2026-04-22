// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataflowEditorToolEnums.generated.h"

UENUM()
enum class EDataflowEditorToolEditMode : uint8
{
	Brush,
	Mesh,
};

UENUM()
enum class EDataflowEditorToolBrushAreaType : uint8
{
	Connected,
	Volumetric
};

UENUM()
enum class EDataflowEditorToolEditOperation : uint8
{
	Add,
	Replace,
	Multiply,
	Invert,
	Relax,
};

UENUM()
enum class EDataflowEditorToolColorMode : uint8
{
	Greyscale,
	Ramp,
	FullMaterial,
};

UENUM()
enum class EDataflowEditorToolVisibilityType : uint8
{
	None,
	Unoccluded,
};

// mirror direction mode
UENUM()
enum class EDataflowEditorToolMirrorDirection : uint8
{
	PositiveToNegative,
	NegativeToPositive,
};
