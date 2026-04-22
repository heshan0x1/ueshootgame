// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

class FMetaHumanCalibrationStyle : public FSlateStyleSet
{
public:

	UE_API FMetaHumanCalibrationStyle();
	UE_API virtual ~FMetaHumanCalibrationStyle() override;

	UE_API static FMetaHumanCalibrationStyle& Get();
};

#undef UE_API