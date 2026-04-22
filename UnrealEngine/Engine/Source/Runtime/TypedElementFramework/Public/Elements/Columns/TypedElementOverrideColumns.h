// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataStorage/CommonTypes.h"
#include "UObject/OverridableManager.h"

#include "TypedElementOverrideColumns.generated.h"

// Whether the object has an override on the base
USTRUCT(meta = (DisplayName = "Override"))
struct FObjectOverrideColumn final : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	EOverriddenState OverriddenState;
};