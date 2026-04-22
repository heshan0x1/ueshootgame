// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioInsightsStyle.h"

namespace UE::Audio::Insights
{
	FSlateStyle& FSlateStyle::Get()
	{
		static FSlateStyle InsightsStyle;
		return InsightsStyle;
	}
}
