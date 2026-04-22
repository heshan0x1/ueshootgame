// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORETECH_API

/************************************************************************/
/* 1 Euro filter smoothing algorithm									*/
/* http://cristal.univ-lille.fr/~casiez/1euro/							*/
/************************************************************************/

// This is a copy of Engine\Source\Editor\ViewportInteraction\Public\ViewportInteractionUtils.h
// but modified to work on single value floats not vectors

class FMetaHumanOneEuroFilter
{
private:

	class FMetaHumanLowpassFilter
	{
	public:

		/** Default constructor */
		FMetaHumanLowpassFilter();

		/** Calculate */
		double Filter(const double InValue, const double InAlpha);

		/** If the filter was not executed yet */
		bool IsFirstTime() const;

		/** Get the previous filtered value */
		double GetPrevious() const;

	private:

		/** The previous filtered value */
		double Previous;

		/** If this is the first time doing a filter */
		bool bFirstTime;
	};

public:

	/** Default constructor */
	UE_API FMetaHumanOneEuroFilter();

	UE_API FMetaHumanOneEuroFilter(const double InMinCutoff, const double InCutoffSlope, const double InDeltaCutoff);

	/** Smooth parameter */
	UE_API double Filter(const double InRaw, const double InDeltaTime);

	/** Set the minimum cutoff */
	UE_API void SetMinCutoff(const double InMinCutoff);

	/** Set the cutoff slope */
	UE_API void SetCutoffSlope(const double InCutoffSlope);

	/** Set the delta slope */
	UE_API void SetDeltaCutoff(const double InDeltaCutoff);

private:

	UE_API const double CalculateCutoff(const double InValue);
	UE_API const double CalculateAlpha(const double InCutoff, const double InDeltaTime) const;

	double MinCutoff;
	double CutoffSlope;
	double DeltaCutoff;
	FMetaHumanLowpassFilter RawFilter;
	FMetaHumanLowpassFilter DeltaFilter;
};

#undef UE_API
