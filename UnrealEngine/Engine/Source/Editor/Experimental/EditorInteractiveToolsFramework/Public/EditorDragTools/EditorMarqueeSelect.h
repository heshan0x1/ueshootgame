// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EditorDragToolBehaviorTarget.h"

class FCanvas;
class FEditorViewportClient;
class FSceneView;
class UModel;

/**
 * Base class for tools that use a marquee selection behavior
 */
class FEditorMarqueeSelect : public FEditorDragToolBehaviorTarget
{
public:
	explicit FEditorMarqueeSelect(FEditorViewportClient* const InEditorViewportClient)
			: FEditorDragToolBehaviorTarget(InEditorViewportClient)
	{
	}

	virtual void Render(const FSceneView* View, FCanvas* Canvas, EViewInteractionState InInteractionState) override;
	
protected:

	/**
	 * @return Whether selection should only include items completely contained by the box.  
	 */
	bool IsWindowSelection() const;
};
