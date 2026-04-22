// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorDragTools/EditorMarqueeSelect.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Settings/LevelEditorViewportSettings.h"

void FEditorMarqueeSelect::Render(const FSceneView* View, FCanvas* Canvas, EViewInteractionState InInteractionState)
{
	if (InInteractionState != EViewInteractionState::Focused)
	{
		return;
	}

	if (!Canvas || !View)
	{
		return;
	}
	
	if (IsWindowSelection())
	{
		FCanvasBoxItem BoxItem(
			FVector2D(Start.X, Start.Y) / Canvas->GetDPIScale(),
			FVector2D(End.X - Start.X, End.Y - Start.Y) / Canvas->GetDPIScale()
		);
	
		BoxItem.SetColor(FLinearColor::White);
		Canvas->DrawItem(BoxItem);
	}
	else
	{
		FVector LocalStart = Start;
		FVector LocalEnd = End;
		
		// Place the starting corner in the upper-left
		if (LocalStart.X > LocalEnd.X)
		{
			Swap(LocalStart.X, LocalEnd.X);
		}

		if (LocalStart.Y > LocalEnd.Y)
		{
			Swap(LocalStart.Y, LocalEnd.Y);
		}

		FCanvasDashedBoxItem BoxItem(
			FVector2D(LocalStart.X, LocalStart.Y) / Canvas->GetDPIScale(),
			FVector2D(LocalEnd.X - LocalStart.X, LocalEnd.Y - LocalStart.Y) / Canvas->GetDPIScale()
		);
		
		BoxItem.DashLength = 12.0f;
		BoxItem.DashGap = 6.0f;
		BoxItem.SetColor(FLinearColor::White);
	
		Canvas->DrawItem(BoxItem);	
	}
}

bool FEditorMarqueeSelect::IsWindowSelection() const
{
	switch (GetDefault<ULevelEditorViewportSettings>()->MarqueeSelectionMode)
	{
	case EMarqueeSelectionMode::Crossing:
		return false;
	case EMarqueeSelectionMode::Window:
		return true;
	case EMarqueeSelectionMode::CrossLeft:
		return Start.X < End.X;
	case EMarqueeSelectionMode::CrossRight:
		return Start.X > End.X;
	default:
		return false;
	}
}
