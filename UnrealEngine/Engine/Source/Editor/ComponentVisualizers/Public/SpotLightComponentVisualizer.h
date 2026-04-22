// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"
#include "CoreMinimal.h"
#include "PointLightComponentVisualizer.h"

#define UE_API COMPONENTVISUALIZERS_API

class FPrimitiveDrawInterface;
class FSceneView;
class UActorComponent;

class FSpotLightComponentVisualizer : public FComponentVisualizer
{
public:
	//~ Begin FComponentVisualizer Interface
	UE_API virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	//~ End FComponentVisualizer Interface

private:
	FTextureLightProfileVisualizer LightProfileVisualizer;
};

#undef UE_API
