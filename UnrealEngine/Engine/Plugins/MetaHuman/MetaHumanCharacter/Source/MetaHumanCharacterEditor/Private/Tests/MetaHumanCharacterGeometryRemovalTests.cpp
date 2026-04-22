// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MetaHumanGeometryRemoval.h"

#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMetaHumanHiddenFaceMapTest, 
	"MetaHuman.Creator.Pipeline.HiddenFaceMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

bool FMetaHumanHiddenFaceMapTest::RunTest(const FString& InParams)
{
	using namespace UE::MetaHuman::GeometryRemoval;

	constexpr uint8 KeepValue = MAX_uint8;
	constexpr uint8 StripValue = 0;

	FHiddenFaceMapImage DestinationMap;

	TArray<FHiddenFaceMapImage> SourceMaps;
	SourceMaps.AddDefaulted(2);
	FHiddenFaceMapImage& SourceA = SourceMaps[0];
	FHiddenFaceMapImage& SourceB = SourceMaps[1];

	// Initialize SourceA to a 1x1 texture with KeepValue
	{
		SourceA.Image.Init(
			1,
			1,
			ERawImageFormat::G8);

		check(SourceA.Image.GetNumPixels() == 1);

		TArrayView64<uint8> Pixels = SourceA.Image.AsG8();
		Pixels[0] = KeepValue;
	}

	// Initialize SourceB to a 1x1 texture with KeepValue
	{
		SourceB.Image.Init(
			1,
			1,
			ERawImageFormat::G8);

		TArrayView64<uint8> Pixels = SourceB.Image.AsG8();
		Pixels[0] = KeepValue;
	}

	FText FailureReason;
	const bool bResult1 = UE::MetaHuman::GeometryRemoval::TryCombineHiddenFaceMaps(SourceMaps, DestinationMap, FailureReason);
	UTEST_TRUE("Two 1x1 maps can be combined", bResult1);
	UTEST_TRUE("Two 1x1 maps produce a 1x1 map", DestinationMap.Image.GetWidth() == 1 && DestinationMap.Image.GetHeight() == 1);

	{
		TArrayView64<uint8> Pixels = DestinationMap.Image.AsG8();
		UTEST_EQUAL("Destination pixel is set to keep if two source pixels are set to keep", Pixels[0], KeepValue);
	}

	// Write StripValue to SourceA
	{
		TArrayView64<uint8> Pixels = SourceA.Image.AsG8();
		Pixels[0] = StripValue;
	}

	const bool bResult2 = UE::MetaHuman::GeometryRemoval::TryCombineHiddenFaceMaps(SourceMaps, DestinationMap, FailureReason);
	UTEST_TRUE("Two 1x1 maps can be combined a second time", bResult2);

	{
		TArrayView64<uint8> Pixels = DestinationMap.Image.AsG8();
		UTEST_EQUAL("Destination pixel is set to strip if at least one source pixel is set to strip", Pixels[0], StripValue);
	}

	// Initialize SourceA to a 1x2 texture with multiple values
	{
		SourceA.Image.Init(
			1,
			2,
			ERawImageFormat::G8);

		check(SourceA.Image.GetNumPixels() == 2);

		TArrayView64<uint8> Pixels = SourceA.Image.AsG8();
		Pixels[0] = KeepValue;
		Pixels[1] = StripValue;
	}

	const bool bResult3 = UE::MetaHuman::GeometryRemoval::TryCombineHiddenFaceMaps(SourceMaps, DestinationMap, FailureReason);
	UTEST_TRUE("A 1x1 and a 1x2 map can be combined", bResult3);
	UTEST_TRUE("A 1x1 and a 1x2 map produce a 1x2 map", DestinationMap.Image.GetWidth() == 1 && DestinationMap.Image.GetHeight() == 2);

	{
		TArrayView64<uint8> Pixels = DestinationMap.Image.AsG8();
		UTEST_EQUAL("Destination pixel set correctly (keep) when one source map pixel covers multiple destination pixels", Pixels[0], KeepValue);
		UTEST_EQUAL("Destination pixel set correctly (strip) when one source map pixel covers multiple destination pixels", Pixels[1], StripValue);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
