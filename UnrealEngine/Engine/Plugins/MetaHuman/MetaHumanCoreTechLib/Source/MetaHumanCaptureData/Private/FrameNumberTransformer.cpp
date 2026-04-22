// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameNumberTransformer.h"

namespace UE::MetaHuman
{

FFrameNumberTransformer::FFrameNumberTransformer() :
	FFrameNumberTransformer::FFrameNumberTransformer(FFrameRate(1, 1), FFrameRate(1, 1), 0)
{
}

FFrameNumberTransformer::FFrameNumberTransformer(const int32 InFrameNumberOffset) :
	FFrameNumberTransformer::FFrameNumberTransformer(FFrameRate(1, 1), FFrameRate(1, 1), InFrameNumberOffset)
{
}

FFrameNumberTransformer::FFrameNumberTransformer(FFrameRate InSourceFrameRate, FFrameRate InTargetFrameRate) :
	FFrameNumberTransformer::FFrameNumberTransformer(MoveTemp(InSourceFrameRate), MoveTemp(InTargetFrameRate), 0)
{
}

FFrameNumberTransformer::FFrameNumberTransformer(FFrameRate InSourceFrameRate, FFrameRate InTargetFrameRate, const int32 InFrameNumberOffset) :
	SourceFrameRate(MoveTemp(InSourceFrameRate)),
	TargetFrameRate(MoveTemp(InTargetFrameRate)),
	FrameNumberOffset(InFrameNumberOffset)
{
	const double SourceFrameRateDecimal = SourceFrameRate.AsDecimal();
	const double TargetFrameRateDecimal = TargetFrameRate.AsDecimal();

	if (ensure(!FMath::IsNearlyZero(TargetFrameRateDecimal)))
	{
		const double FrameRateRatio = SourceFrameRateDecimal / TargetFrameRateDecimal;
		const double AbsoluteFrameRateRatio = FMath::Abs(FrameRateRatio);

		if (ensure(!FMath::IsNearlyZero(AbsoluteFrameRateRatio)))
		{
			SkipFactor = FMath::RoundToInt32(AbsoluteFrameRateRatio);
			const double DuplicationFactorRatio = 1.0 / AbsoluteFrameRateRatio;
			DuplicationFactor = FMath::RoundToInt32(DuplicationFactorRatio);
		}
	}
}

int32 FFrameNumberTransformer::Transform(const int32 InFrameNumber) const
{
	double NewFrameNumber = FrameNumberOffset + InFrameNumber;

	if (DuplicationFactor > 1)
	{
		NewFrameNumber /= DuplicationFactor;
	}
	else if (SkipFactor > 1)
	{
		NewFrameNumber *= SkipFactor;
	}

	return FMath::TruncToInt32(NewFrameNumber);
}

}
