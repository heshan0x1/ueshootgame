// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ImageCore.h"
#include "Interfaces/IPluginManager.h"
#include "Math/Color.h"
#include "Misc/AutomationTest.h"
#include "Stats/StatsMisc.h"

#include "MetaHumanFaceTextureSynthesizer.h"

//#define METAHUMAN_TEXTURESYNTHESIS_TEST_SAVEOUTPUT
#ifdef METAHUMAN_TEXTURESYNTHESIS_TEST_SAVEOUTPUT
#include "ImageUtils.h"
#include "Misc/Paths.h"
#endif


namespace UE::MetaHuman
{
	static FString GetTestModelPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
		if (!Plugin.IsValid())
		{
	 		return FString{};
		}
	 
		const FString TextureSynthesisModelDir = Plugin->GetContentDir() + TEXT("/Optional/TextureSynthesis");
		const FString ModelName(TEXT("TS-1.3-E_UE_res-1024_nchr-153"));

		return TextureSynthesisModelDir / ModelName;
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanFaceTextureSynthesizerTest, "MetaHumanCreator.TextureSynthesis.FaceTextureSynthesizerTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMetaHumanFaceTextureSynthesizerTest::RunTest(const FString& InParams)
{
	// Test model texture size
	const int32 ExpectedModelResolution = 1024;

	FMetaHumanFaceTextureSynthesizer FaceTextureSynthesizer;

	// Initialize the synthesizer
	UTEST_TRUE("Face Texture Synthesizer initialization", FaceTextureSynthesizer.Init(UE::MetaHuman::GetTestModelPath()));
	UTEST_VALID("Face Texture Synthesizer is valid", FaceTextureSynthesizer);
	UTEST_TRUE("Face Texure Synthesizer IsValid", FaceTextureSynthesizer.IsValid());

	// Test for the test model resolution
	UTEST_EQUAL("Face Texture Synthesizer size X", FaceTextureSynthesizer.GetTextureSizeX(), ExpectedModelResolution);
	UTEST_EQUAL("Face Texture Synthesizer size Y", FaceTextureSynthesizer.GetTextureSizeY(), ExpectedModelResolution);

	const FMetaHumanFaceTextureSynthesizer::FTextureSynthesisParams TextureSynthesisParams{
		.SkinUVFromUI = FVector2f{ 0.5f, 0.5f },
		.HighFrequencyIndex = 0,
		.MapType = FMetaHumanFaceTextureSynthesizer::EMapType::Base
	};

	// Smoke tests for synthesize functions
	FImage OutAlbedo;
	OutAlbedo.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	UTEST_TRUE("Synthesize albedo map", FaceTextureSynthesizer.SynthesizeAlbedo(TextureSynthesisParams, OutAlbedo));
	UTEST_EQUAL("Synthesized albedo size X", OutAlbedo.SizeX, FaceTextureSynthesizer.GetTextureSizeX());
	UTEST_EQUAL("Synthesized albedo size Y", OutAlbedo.SizeY, FaceTextureSynthesizer.GetTextureSizeY());
	UTEST_EQUAL("Synthesized albedo pixel format", OutAlbedo.Format, FaceTextureSynthesizer.GetTextureFormat());
	UTEST_EQUAL("Synthesized albedo Gamma space", OutAlbedo.GammaSpace, FaceTextureSynthesizer.GetTextureColorSpace());

	FImage OutNormal;
	OutNormal.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	UTEST_TRUE("Synthesize normal map", FaceTextureSynthesizer.SelectNormal(TextureSynthesisParams, OutNormal));
	UTEST_EQUAL("Synthesized normal size X", OutNormal.SizeX, FaceTextureSynthesizer.GetTextureSizeX());
	UTEST_EQUAL("Synthesized normal size Y", OutNormal.SizeY, FaceTextureSynthesizer.GetTextureSizeY());
	UTEST_EQUAL("Synthesized normal pixel format", OutNormal.Format, FaceTextureSynthesizer.GetTextureFormat());
	UTEST_EQUAL("Synthesized normal Gamma space", OutNormal.GammaSpace, FaceTextureSynthesizer.GetTextureColorSpace());

	FImage OutCavity;
	OutCavity.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	UTEST_TRUE("Synthesize normal map", FaceTextureSynthesizer.SelectCavity(0, OutCavity));
	UTEST_EQUAL("Synthesized cavity size X", OutCavity.SizeX, FaceTextureSynthesizer.GetTextureSizeX());
	UTEST_EQUAL("Synthesized cavity size Y", OutCavity.SizeY, FaceTextureSynthesizer.GetTextureSizeY());
	UTEST_EQUAL("Synthesized cavity pixel format", OutCavity.Format, FaceTextureSynthesizer.GetTextureFormat());
	UTEST_EQUAL("Synthesized cavity Gamma space", OutCavity.GammaSpace, FaceTextureSynthesizer.GetTextureColorSpace());

#ifdef METAHUMAN_TEXTURESYNTHESIS_TEST_SAVEOUTPUT
	UTEST_TRUE("Save synthesized albedo image", FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanTextureSynthesisTests_OutAlbedo.png"))), OutAlbedo));
	UTEST_TRUE("Save synthesized normal image", FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanTextureSynthesisTests_OutNormal.png"))), OutNormal));
	UTEST_TRUE("Save synthesized cavity image", FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanTextureSynthesisTests_OutCavity.png"))), OutCavity));
#endif

	// Smoke tests for various simple functions
	const int32 MaxHFIndex = FaceTextureSynthesizer.GetMaxHighFrequencyIndex();
	UTEST_EQUAL("Max high frequency index", MaxHFIndex, 153);

	const TArray<FMetaHumanFaceTextureSynthesizer::EMapType> AlbedoMapTypes = FaceTextureSynthesizer.GetSupportedAlbedoMapTypes();
	UTEST_EQUAL("Number of supported albedo map types", AlbedoMapTypes.Num(), 1);
	UTEST_EQUAL("Albedo map type", AlbedoMapTypes[0], FMetaHumanFaceTextureSynthesizer::EMapType::Base);

	const TArray<FMetaHumanFaceTextureSynthesizer::EMapType> NormalMapTypes = FaceTextureSynthesizer.GetSupportedNormalMapTypes();
	UTEST_EQUAL("Number of supported normal map types", NormalMapTypes.Num(), 1);
	UTEST_EQUAL("Normal map type", NormalMapTypes[0], FMetaHumanFaceTextureSynthesizer::EMapType::Base);

	// Smoke tests for various skin tone functions (no values checked in these tests)
	const FVector2f SkinUV = { 0.3f, 0.7f};

	const FLinearColor LinearColorSkinTone = FaceTextureSynthesizer.GetSkinTone(SkinUV);
	FVector3f Bias, Gain;
	Bias[0] = FMath::Pow(LinearColorSkinTone.R, 2.2) * 256;
	Bias[1] = FMath::Pow(LinearColorSkinTone.G, 2.2) * 256;
	Bias[2] = FMath::Pow(LinearColorSkinTone.B, 2.2) * 256;
	Gain = FaceTextureSynthesizer.GetBodyAlbedoGain(SkinUV);

	const FVector2f ProjectedSkinTone = FaceTextureSynthesizer.ProjectSkinTone(LinearColorSkinTone, /*HFIndex*/ 5);

	// Smoke test GetFaceTextureAttributeMap
	const FMetaHumanFaceTextureAttributeMap& FaceTextureAttributeMap = FaceTextureSynthesizer.GetFaceTextureAttributeMap();
	const int32 ExpectedNumAttributes = 3;
	UTEST_EQUAL("Check expected number of attributes", FaceTextureAttributeMap.NumAttributes(), ExpectedNumAttributes);
	UTEST_EQUAL("Check expected attribute name 0", FaceTextureAttributeMap.GetAttributeName(0), FString("Face Wrinkles"));
	UTEST_EQUAL("Check expected attribute name 1", FaceTextureAttributeMap.GetAttributeName(1), FString("Face Stubble"));
	UTEST_EQUAL("Check expected attribute name 2", FaceTextureAttributeMap.GetAttributeName(2), FString("Face Marks"));
	const TArray<FString>& AttributeValueNames = FaceTextureAttributeMap.GetAttributeValueNames(0);
	UTEST_EQUAL("Check AttributeValueNames", AttributeValueNames.Num(), 3);
	UTEST_EQUAL("Check AttributeValueNames 0", AttributeValueNames[0], FString("Low"));
	UTEST_EQUAL("Check AttributeValueNames 1", AttributeValueNames[1], FString("Medium"));
	UTEST_EQUAL("Check AttributeValueNames 2", AttributeValueNames[2], FString("High"));
	const TArray<int32>& AttributeValues = FaceTextureAttributeMap.GetAttributeValues(2);
	UTEST_EQUAL("Check AttributeValues", AttributeValues.Num(), 153);
	const TArray<int32>& AllIndices = FaceTextureAttributeMap.GetAllIndices();
	UTEST_EQUAL("Check AllIndices", AllIndices.Num(), 153);
	TArray<int32> FilteredIndices = FaceTextureAttributeMap.Filter(/*AttributeIndex*/ 1, /*AttributeValue*/ 1, AllIndices);
	UTEST_EQUAL("Check Filter", FilteredIndices.Num(), 23);


	// Smoke test for titan memory clean up
	FaceTextureSynthesizer.Clear();

	// TODO Cannot easily test SynthesizeAlbedoWithHF as it is used by the service API and we cannot do authentication yet in a unit test

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetaHumanFaceTextureSynthesizerPerfTest, "MetaHumanCreator.TextureSynthesis.FaceTextureSynthesizerPerfTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMetaHumanFaceTextureSynthesizerPerfTest::RunTest(const FString& InParams)
{
	// Simple timing performance test for the synthesize functions

	// Initialize the synthesizer
	FMetaHumanFaceTextureSynthesizer FaceTextureSynthesizer;
	UTEST_TRUE("Face Texture Synthesizer initialization", FaceTextureSynthesizer.Init(UE::MetaHuman::GetTestModelPath()));
	UTEST_VALID("Face Texture Synthesizer is valid", FaceTextureSynthesizer);

	const FMetaHumanFaceTextureSynthesizer::FTextureSynthesisParams TextureSynthesisParams{
		.SkinUVFromUI = FVector2f{ 0.5f, 0.5f },
		.HighFrequencyIndex = 0,
		.MapType = FMetaHumanFaceTextureSynthesizer::EMapType::Base
	};

	// Timing tests
	bool bSynthesizeResult = true;
	FImage OutAlbedo;
	OutAlbedo.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	{
		// Time only the synthesize call		
		FScopeLogTime LogTimePtr(TEXT("FMetaHumanFaceTextureSynthesizer::SynthesizeAlbedo"),nullptr, FScopeLogTime::ScopeLog_Milliseconds);
		bSynthesizeResult = FaceTextureSynthesizer.SynthesizeAlbedo(TextureSynthesisParams, OutAlbedo);
	}
	// Check call was valid without errors
	UTEST_TRUE("Synthesize albedo map", bSynthesizeResult);

	FImage OutNormal;
	OutNormal.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	{
		FScopeLogTime LogTimePtr(TEXT("FMetaHumanFaceTextureSynthesizer::SelectNormal"), nullptr, FScopeLogTime::ScopeLog_Milliseconds);
		bSynthesizeResult = FaceTextureSynthesizer.SelectNormal(TextureSynthesisParams, OutNormal);
	}
	// Check call was valid without errors
	UTEST_TRUE("Synthesize normal map", bSynthesizeResult);

	FImage OutCavity;
	OutCavity.Init(FaceTextureSynthesizer.GetTextureSizeX(), FaceTextureSynthesizer.GetTextureSizeY(), FaceTextureSynthesizer.GetTextureFormat(), FaceTextureSynthesizer.GetTextureColorSpace());
	{
		FScopeLogTime LogTimePtr(TEXT("FMetaHumanFaceTextureSynthesizer::SelectCavity"), nullptr, FScopeLogTime::ScopeLog_Milliseconds);
		bSynthesizeResult = FaceTextureSynthesizer.SelectCavity(0, OutCavity);
	}
	// Check call was valid without errors
	UTEST_TRUE("Select cavity map", bSynthesizeResult);

	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
