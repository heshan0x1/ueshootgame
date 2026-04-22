// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"

#include "PVFloatRamp.generated.h"

USTRUCT(BlueprintType)
struct PROCEDURALVEGETATION_API FPVFloatRamp
{
	GENERATED_BODY()

	UPROPERTY()
	FRichCurve EditorCurveData;

	FRichCurve* GetRichCurve()
	{
		return &EditorCurveData;
	}
	const FRichCurve* GetRichCurveConst() const
	{
		return &EditorCurveData;
	}
};
