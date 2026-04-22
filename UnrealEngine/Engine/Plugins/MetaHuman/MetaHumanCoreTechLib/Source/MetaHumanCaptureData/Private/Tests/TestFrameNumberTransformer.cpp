// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameNumberTransformer.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman
{
struct FFrameRatePairUnderTest
{
	FFrameRate SourceFrameRate;
	FFrameRate TargetFrameRate;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_NoMapping,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.NoTransform",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_NoMapping::RunTest(const FString& InParameters)
{
	FFrameNumberTransformer FrameNumberTransformer;

	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(0), 0);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(1), 1);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(2), 2);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(3), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_SimpleOffset,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.SimpleOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_SimpleOffset::RunTest(const FString& InParameters)
{
	constexpr int32 FrameNumberOffset = 2;
	FFrameNumberTransformer FrameNumberTransformer(FrameNumberOffset);

	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(0), 2);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(1), 3);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(2), 4);
	UTEST_EQUAL("Frame Number", FrameNumberTransformer.Transform(3), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_TargetRate2xSourceRate,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.TargetRate2xSourceRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_TargetRate2xSourceRate::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(12'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(48'000, 1'000)},
		{.SourceFrameRate = FFrameRate(25'000, 1'000), .TargetFrameRate = FFrameRate(50'000, 1'000)},
		{.SourceFrameRate = FFrameRate(30'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(120'000, 1'000)},

		{.SourceFrameRate = FFrameRate(24'000, 1'001), .TargetFrameRate = FFrameRate(48'000, 1'001)},
		{.SourceFrameRate = FFrameRate(30'000, 1'001), .TargetFrameRate = FFrameRate(60'000, 1'001)},
	};

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 0);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 0);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 1);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 1);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 2);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_SourceRate2xTargetRate,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.SourceRate2xTargetRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_SourceRate2xTargetRate::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(12'000, 1'000)},
		{.SourceFrameRate = FFrameRate(48'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(50'000, 1'000), .TargetFrameRate = FFrameRate(25'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(30'000, 1'000)},
		{.SourceFrameRate = FFrameRate(120'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},

		{.SourceFrameRate = FFrameRate(48'000, 1'001), .TargetFrameRate = FFrameRate(24'000, 1'001)},
		{.SourceFrameRate = FFrameRate(60'000, 1'001), .TargetFrameRate = FFrameRate(30'000, 1'001)},
	};

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 0);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 2);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 4);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 6);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 8);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 10);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_SourceRate2xTargetRateWithOffset,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.SourceRate2xTargetRateWithOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_SourceRate2xTargetRateWithOffset::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(12'000, 1'000)},
		{.SourceFrameRate = FFrameRate(48'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(50'000, 1'000), .TargetFrameRate = FFrameRate(25'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(30'000, 1'000)},
		{.SourceFrameRate = FFrameRate(120'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},

		{.SourceFrameRate = FFrameRate(48'000, 1'001), .TargetFrameRate = FFrameRate(24'000, 1'001)},
		{.SourceFrameRate = FFrameRate(60'000, 1'001), .TargetFrameRate = FFrameRate(30'000, 1'001)},
	};

	constexpr int32 FrameNumberOffset = 3;

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate, FrameNumberOffset);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 6); // 0 -> 3 * 2
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 8); // 1 -> 4 * 2
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 10);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 12);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 14);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 16);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_TargetRate2xSourceRateWithOffset,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.TargetRate2xSourceRateWithOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_TargetRate2xSourceRateWithOffset::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(12'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(48'000, 1'000)},
		{.SourceFrameRate = FFrameRate(25'000, 1'000), .TargetFrameRate = FFrameRate(50'000, 1'000)},
		{.SourceFrameRate = FFrameRate(30'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(120'000, 1'000)},

		{.SourceFrameRate = FFrameRate(24'000, 1'001), .TargetFrameRate = FFrameRate(48'000, 1'001)},
		{.SourceFrameRate = FFrameRate(30'000, 1'001), .TargetFrameRate = FFrameRate(60'000, 1'001)},
	};

	constexpr int32 FrameNumberOffset = 3;

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate, FrameNumberOffset);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 1); // 0 -> 3 / 2 Floored
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 2); // 1 -> 4 / 2 Floored
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 2); // 2 -> 5 / 2 Floored
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 3);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 3);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 4);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_RatesEqual,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.RatesEqual",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_RatesEqual::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(12'000, 1'000), .TargetFrameRate = FFrameRate(12'000, 1'000)},
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(25'000, 1'000), .TargetFrameRate = FFrameRate(25'000, 1'000)},
		{.SourceFrameRate = FFrameRate(30'000, 1'000), .TargetFrameRate = FFrameRate(30'000, 1'000)},
		{.SourceFrameRate = FFrameRate(48'000, 1'000), .TargetFrameRate = FFrameRate(48'000, 1'000)},
		{.SourceFrameRate = FFrameRate(50'000, 1'000), .TargetFrameRate = FFrameRate(50'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},
		{.SourceFrameRate = FFrameRate(120'000, 1'000), .TargetFrameRate = FFrameRate(120'000, 1'000)},

		{.SourceFrameRate = FFrameRate(24'000, 1'001), .TargetFrameRate = FFrameRate(24'000, 1'001)},
		{.SourceFrameRate = FFrameRate(30'000, 1'001), .TargetFrameRate = FFrameRate(30'000, 1'001)},
		{.SourceFrameRate = FFrameRate(60'000, 1'001), .TargetFrameRate = FFrameRate(60'000, 1'001)},
	};

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 0);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 1);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 2);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 3);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 4);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 5);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFrameNumberTransformerTest_RatesEqualWithOffset,
	"MetaHuman.Capture.CaptureData.FrameNumberTransformer.RatesEqualWithOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter
)
bool FFrameNumberTransformerTest_RatesEqualWithOffset::RunTest(const FString& InParameters)
{
	const TArray<FFrameRatePairUnderTest> PairsUnderTest = {
		{.SourceFrameRate = FFrameRate(12'000, 1'000), .TargetFrameRate = FFrameRate(12'000, 1'000)},
		{.SourceFrameRate = FFrameRate(24'000, 1'000), .TargetFrameRate = FFrameRate(24'000, 1'000)},
		{.SourceFrameRate = FFrameRate(25'000, 1'000), .TargetFrameRate = FFrameRate(25'000, 1'000)},
		{.SourceFrameRate = FFrameRate(30'000, 1'000), .TargetFrameRate = FFrameRate(30'000, 1'000)},
		{.SourceFrameRate = FFrameRate(48'000, 1'000), .TargetFrameRate = FFrameRate(48'000, 1'000)},
		{.SourceFrameRate = FFrameRate(50'000, 1'000), .TargetFrameRate = FFrameRate(50'000, 1'000)},
		{.SourceFrameRate = FFrameRate(60'000, 1'000), .TargetFrameRate = FFrameRate(60'000, 1'000)},
		{.SourceFrameRate = FFrameRate(120'000, 1'000), .TargetFrameRate = FFrameRate(120'000, 1'000)},

		{.SourceFrameRate = FFrameRate(24'000, 1'001), .TargetFrameRate = FFrameRate(24'000, 1'001)},
		{.SourceFrameRate = FFrameRate(30'000, 1'001), .TargetFrameRate = FFrameRate(30'000, 1'001)},
		{.SourceFrameRate = FFrameRate(60'000, 1'001), .TargetFrameRate = FFrameRate(60'000, 1'001)},
	};

	constexpr int32 FrameNumberOffset = 3;

	for (const FFrameRatePairUnderTest& PairUnderTest : PairsUnderTest)
	{
		FFrameNumberTransformer FrameNumberTransformer(PairUnderTest.SourceFrameRate, PairUnderTest.TargetFrameRate, FrameNumberOffset);

		const FString FailureMessage = FString::Printf(
			TEXT("Frame number (SourceFrameRate=%.2f, TargetFrameRate=%.2f)"),
			PairUnderTest.SourceFrameRate.AsDecimal(),
			PairUnderTest.TargetFrameRate.AsDecimal()
		);

		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(0), 3);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(1), 4);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(2), 5);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(3), 6);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(4), 7);
		UTEST_EQUAL(FailureMessage, FrameNumberTransformer.Transform(5), 8);
	}

	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS