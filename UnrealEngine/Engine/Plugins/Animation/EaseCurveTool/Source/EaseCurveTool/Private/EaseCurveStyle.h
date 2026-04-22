// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"

class FEaseCurveStyle final : public FSlateStyleSet
{
public:
	static FEaseCurveStyle& Get()
	{
		static FEaseCurveStyle Instance;
		return Instance;
	}

	FEaseCurveStyle();
	virtual ~FEaseCurveStyle() override;
};
