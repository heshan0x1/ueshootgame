// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"

#define UE_API SMARTOBJECTSEDITORMODULE_API

class USmartObjectComponent;
class USmartObjectDefinition;
class USmartObjectAssetEditorTool;
class USmartObjectSubsystem;

/**
 * Hit proxy for Smart Object slots and annotations.
 */
struct HSmartObjectItemProxy : public HHitProxy
{
	DECLARE_HIT_PROXY( UE_API );

	HSmartObjectItemProxy(const FGuid InID)
		: HHitProxy(HPP_Foreground)
		, ItemID(InID)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::CardinalCross;
	}

	FGuid ItemID;
};

/**
 * Helper functions to draw Smart Object definition visualization.
 */
namespace UE::SmartObject::Editor
{
	void Draw(const USmartObjectDefinition& Definition, TConstArrayView<FGuid> Selection, const FTransform& OwnerLocalToWorld, const FSceneView& View, FPrimitiveDrawInterface& PDI, const UWorld& World, const AActor* PreviewActor);
	void DrawCanvas(const USmartObjectDefinition& Definition, TConstArrayView<FGuid> Selection, const FTransform& OwnerLocalToWorld, const FSceneView& View, FCanvas& Canvas, const UWorld& World, const AActor* PreviewActor);
}; // UE::SmartObject::Editor


/**
 * Visualizer for SmartObjectComponent
 */
class FSmartObjectComponentVisualizer : public FComponentVisualizer
{
protected:
	UE_API virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	UE_API virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
};

#undef UE_API
