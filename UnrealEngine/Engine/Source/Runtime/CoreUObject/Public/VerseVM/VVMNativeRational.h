// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Serialization/StructuredArchive.h"
#include "Templates/TypeHash.h"
#include "VVMNativeRational.generated.h"

// Used to represent integer rational numbers in Verse.
USTRUCT()
struct FVerseRational
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Numerator{0};

	UPROPERTY()
	int64 Denominator{0};

	FVerseRational ReduceAndNormalizeSigns() const
	{
		int64 A = Numerator;
		int64 B = Denominator;
		while (B)
		{
			int64 Remainder = A % B;
			A = B;
			B = Remainder;
		}

		FVerseRational Result;
		Result.Numerator = Numerator / A;
		Result.Denominator = Denominator / A;

		if (Result.Denominator < 0 && Result.Numerator != INT64_MIN && Result.Denominator != INT64_MIN)
		{
			Result.Numerator = -Result.Numerator;
			Result.Denominator = -Result.Denominator;
		}

		return Result;
	}

	friend bool operator==(const FVerseRational& A, const FVerseRational& B)
	{
		FVerseRational ReducedA = A.ReduceAndNormalizeSigns();
		FVerseRational ReducedB = B.ReduceAndNormalizeSigns();
		return ReducedA.Numerator == ReducedB.Numerator
			&& ReducedA.Denominator == ReducedB.Denominator;
	}

	friend uint32 GetTypeHash(const FVerseRational& R)
	{
		FVerseRational ReducedR = R.ReduceAndNormalizeSigns();
		// Make sure that integers hash the same whether represented as an int64 or a FVerseRational.
		const uint32 NumeratorHash = GetTypeHash(R.Numerator);
		return R.Denominator == 1
				 ? NumeratorHash
				 : HashCombineFast(NumeratorHash, GetTypeHash(R.Denominator));
	}

	friend void operator<<(FStructuredArchive::FSlot Slot, FVerseRational& R)
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		FStructuredArchive::FSlot NumeratorField = Record.EnterField(TEXT("Numerator"));
		NumeratorField << R.Numerator;
		FStructuredArchive::FSlot DenominatorField = Record.EnterField(TEXT("Denominator"));
		DenominatorField << R.Denominator;
	}
};
