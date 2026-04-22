// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VVMVerseConstraints.generated.h"

USTRUCT()
struct FVerseIntConstraints
{
	GENERATED_BODY()

	UPROPERTY()
	TOptional<int64> ClampMin;

	UPROPERTY()
	TOptional<int64> ClampMax;
};

USTRUCT()
struct FVerseDoubleConstraints
{
	GENERATED_BODY()

	// See CFloatType::IsIntrinsicFloatType
	UPROPERTY()
	double ClampMin = -INFINITY;

	UPROPERTY()
	double ClampMax = NAN;
};
