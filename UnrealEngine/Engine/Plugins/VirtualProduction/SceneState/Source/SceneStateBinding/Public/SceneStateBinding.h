// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyBindingBinding.h"
#include "SceneStateBindingDataHandle.h"
#include "SceneStateBinding.generated.h"

/** Represents a binding from a source property to a destination property */
USTRUCT()
struct FSceneStateBinding : public FPropertyBindingBinding
{
	GENERATED_BODY()

	using FPropertyBindingBinding::FPropertyBindingBinding;

	//~ Begin FPropertyBindingBinding
	SCENESTATEBINDING_API virtual FConstStructView GetSourceDataHandleStruct() const override;
	//~ End FPropertyBindingBinding

	/** Handle to the data containing the source property */
	UPROPERTY()
	FSceneStateBindingDataHandle SourceDataHandle;

	/** Handle to the data containing the target property */
	UPROPERTY()
	FSceneStateBindingDataHandle TargetDataHandle;
};
