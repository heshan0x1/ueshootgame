// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Utils/MetaHumanCalibrationFrameResolver.h"
#include "ParseTakeUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::MetaHuman
{

namespace Private
{

static FMetaHumanCalibrationFrameResolver::FCameraDescriptor CreateCameraDescriptor(FString InTimecode, double InFrameRate, int32 InNumberOfTestData)
{
	TArray<FString> TestData;
	for (int32 Index = 0; Index < InNumberOfTestData; ++Index)
	{
		TestData.Add(FString::FromInt(Index));
	}

	return FMetaHumanCalibrationFrameResolver::FCameraDescriptor(MoveTemp(TestData),
																 ParseTimecode(InTimecode),
																 ConvertFrameRate(InFrameRate));
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolverTimecodeDiff, "MetaHuman.Calibration.FrameResolver.DifferentTimecode.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolverTimecodeDiff::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 = 
		Private::CreateCameraDescriptor(TEXT("00:00:00:01"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = 4;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);
	ExpectedStereoFramePaths[0].FirstCamera = TEXT("0");
	ExpectedStereoFramePaths[0].SecondCamera = TEXT("1");

	ExpectedStereoFramePaths[1].FirstCamera = TEXT("1");
	ExpectedStereoFramePaths[1].SecondCamera = TEXT("2");

	ExpectedStereoFramePaths[2].FirstCamera = TEXT("2");
	ExpectedStereoFramePaths[2].SecondCamera = TEXT("3");

	ExpectedStereoFramePaths[3].FirstCamera = TEXT("3");
	ExpectedStereoFramePaths[3].SecondCamera = TEXT("4");

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolverTimecodeSame, "MetaHuman.Calibration.FrameResolver.SameTimecode.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolverTimecodeSame::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = NumberOfTestData;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);

	for (int32 Index = 0; Index < NumberOfTestData; ++Index)
	{
		ExpectedStereoFramePaths[Index].FirstCamera = FString::FromInt(Index);
		ExpectedStereoFramePaths[Index].SecondCamera = FString::FromInt(Index);
	}

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolverTimecodeAndFrameRateDiff, "MetaHuman.Calibration.FrameResolver.DifferentTimecodeAndFrameRate.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolverTimecodeAndFrameRateDiff::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:01"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 30.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = 2;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);
	ExpectedStereoFramePaths[0].FirstCamera = TEXT("1");
	ExpectedStereoFramePaths[0].SecondCamera = TEXT("1");

	ExpectedStereoFramePaths[1].FirstCamera = TEXT("3");
	ExpectedStereoFramePaths[1].SecondCamera = TEXT("2");

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolver_SameTimecodeAndDiffFrameRate, "MetaHuman.Calibration.FrameResolver.SameTimecodeAndFrameRate.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolver_SameTimecodeAndDiffFrameRate::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:00"), 30.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = 3;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		ExpectedStereoFramePaths[Index].FirstCamera = FString::FromInt(Index * 2);
		ExpectedStereoFramePaths[Index].SecondCamera = FString::FromInt(Index);
	}

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolverTimecodeAndFrameRateDiff_FrameNumberTest, "MetaHuman.Calibration.FrameResolver.DifferentTimecodeAndFrameRate.FrameNumber.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolverTimecodeAndFrameRateDiff_FrameNumberTest::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:41"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:20"), 30.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = 2;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);
	ExpectedStereoFramePaths[0].FirstCamera = TEXT("1");
	ExpectedStereoFramePaths[0].SecondCamera = TEXT("1");

	ExpectedStereoFramePaths[1].FirstCamera = TEXT("3");
	ExpectedStereoFramePaths[1].SecondCamera = TEXT("2");

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalibrationFrameResolverTimecodeAndFrameRateDiff_SameFrameNumberTest, "MetaHuman.Calibration.FrameResolver.SameTimecodeAndFrameRate.SameFrameNumber.Success", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FCalibrationFrameResolverTimecodeAndFrameRateDiff_SameFrameNumberTest::RunTest(const FString& InParameters)
{
	// Prepare test
	static constexpr int32 NumberOfTestData = 5;
	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera1 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:40"), 60.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver::FCameraDescriptor Camera2 =
		Private::CreateCameraDescriptor(TEXT("00:00:00:20"), 30.0, NumberOfTestData);

	FMetaHumanCalibrationFrameResolver Resolver(Camera1, Camera2);

	TArray<FString> Camera1Data;
	UTEST_TRUE(TEXT("Camera 1 valid"), Resolver.GetFramePathsForCameraIndex(0, Camera1Data));

	TArray<FString> Camera2Data;
	UTEST_TRUE(TEXT("Camera 2 valid"), Resolver.GetFramePathsForCameraIndex(1, Camera2Data));

	static constexpr int32 ExpectedNumberOfTestData = 3;
	UTEST_EQUAL(TEXT("Camera 1 Num"), Camera1Data.Num(), ExpectedNumberOfTestData);
	UTEST_EQUAL(TEXT("Camera 2 Num"), Camera2Data.Num(), ExpectedNumberOfTestData);

	TArray<FMetaHumanCalibrationFramePaths> ExpectedStereoFramePaths;
	ExpectedStereoFramePaths.AddDefaulted(ExpectedNumberOfTestData);

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		ExpectedStereoFramePaths[Index].FirstCamera = FString::FromInt(Index * 2);
		ExpectedStereoFramePaths[Index].SecondCamera = FString::FromInt(Index);
	}

	for (int32 Index = 0; Index < ExpectedNumberOfTestData; ++Index)
	{
		FMetaHumanCalibrationFramePaths StereoFramePaths;
		UTEST_TRUE(TEXT("Valid Frame"), Resolver.GetCalibrationFramePathsForFrameIndex(Index, StereoFramePaths));

		// No need for macro as we don't want to break early
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.FirstCamera, ExpectedStereoFramePaths[Index].FirstCamera);
		TestEqual(FString::Printf(TEXT("Frame %d"), Index),
				  StereoFramePaths.SecondCamera, ExpectedStereoFramePaths[Index].SecondCamera);
	}

	return true;
}

}

#endif // WITH_DEV_AUTOMATION_TESTS