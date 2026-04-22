// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/DelayedDrag.h"
#include "Layout/SlateRect.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class FMetaHumanCalibrationAreaSelection : public FDelayedDrag
{
public:

	using FDelayedDrag::FDelayedDrag;

	UE_API void OnStart(const FVector2D& InStartingPosition);
	UE_API void OnUpdate(const FVector2D& InNewPosition);
	UE_API FSlateRect OnEnd(const FVector2D& InLastPosition);

	UE_API void OnDraw(const FGeometry& InAllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 InPaintOnLayerId) const;

private:

	FVector2D StartingPosition;
	FSlateRect AreaOfInterest;
};

#undef UE_API