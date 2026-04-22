// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MetaHumanSDKEditor.h"
#include "MetaHumanTypesEditor.h"

#include "FileUtilities/ZipArchiveReader.h"
#include "FileUtilities/ZipArchiveWriter.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Logging/StructuredLog.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProjectUtilities/MetaHumanAssetManager.h"

using namespace UE::MetaHuman;

namespace UE::MetaHuman::Test
{
void CheckSourceMetaHuman(FSourceMetaHuman Source, const FString CharacterName, const FMetaHumanVersion Version, bool bIsUefn, EMetaHumanQualityLevel QualityLevel, const FString SourceAssetsPath)
{
	if (Source.GetName() != CharacterName)
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Name wrong seen:{seen} expected:{expected}", Source.GetName(), CharacterName);
	}

	if (Source.IsUEFN() != bIsUefn)
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "UEFN status wrong seen:{seen} expected:{expected}", Source.IsUEFN(), bIsUefn);
	}

	if (Source.GetQualityLevel() != QualityLevel)
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "QualityLevel wrong seen:{seen} expected:{expected}", int(Source.GetQualityLevel()), int(QualityLevel));
	}

	if (Source.GetSourceAssetsPath() != SourceAssetsPath)
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "SourceAssetsPath wrong seen:{seen} expected:{expected}", Source.GetSourceAssetsPath(), SourceAssetsPath);
	}

	if (Source.GetVersion() != Version)
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Version wrong seen:{seen} expected:{expected}", Source.GetVersion().AsString(), Version.AsString());
	}
}

void GenerateAndLoadSourceMetaHumanFromFiles(FString Root, bool bIsUefn, EMetaHumanQualityLevel QualityLevel)
{
	// Sample test data
	const FString CharacterName = TEXT("Danielle");
	const FMetaHumanVersion Version(1, 2, 3);

	//Fake structure
	const FString CharacterRoot = Root / TEXT("MetaHumans") / CharacterName;
	const FString CommonRoot = Root / TEXT("MetaHumans/Common");
	const FString SourceAssetsPath = FPaths::ConvertRelativePathToFull(CharacterRoot / TEXT("SourceAssets"));

	// The one file that we actually read
	FString VersionFile = TEXT("{ \"MetaHumanVersion\": \""+ Version.AsString() +"\" }");
	FFileHelper::SaveStringToFile(VersionFile, *(CharacterRoot / TEXT("VersionInfo.txt")));

	// Load
	FSourceMetaHuman FilesSource(CharacterRoot, CommonRoot, CharacterName);

	// Check
	CheckSourceMetaHuman(FilesSource, CharacterName, Version, bIsUefn, QualityLevel, SourceAssetsPath);
}

void GenerateAndLoadSourceMetaHumanFromArchive(FString Root, bool bIsUefn, EMetaHumanQualityLevel QualityLevel)
{
	FString TestArchiveFilename = Root / TEXT("TestArchive.mhpkg");

	// Sample test data
	const FString CharacterName = TEXT("Danielle");
	const FMetaHumanVersion Version(1, 2, 3);

	//Fake structure
	const FString SourceAssetsPath = CharacterName / TEXT("SourceAssets");
	// Write Test Archive
	{
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Root);
		IFileHandle* ArchiveFile = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*TestArchiveFilename);
		FZipArchiveWriter ArchiveWriter(ArchiveFile);

		FString VersionFile = TEXT("{ \"MetaHumanVersion\": \""+ Version.AsString() +"\" }");
		TStringConversion<TStringConvert<TCHAR, char>> VersionConvert = StringCast<ANSICHAR>(*VersionFile);
		const TConstArrayView<uint8> VersionData(reinterpret_cast<const uint8*>(VersionConvert.Get()), VersionConvert.Length());
		ArchiveWriter.AddFile(CharacterName / TEXT("VersionInfo.txt"), VersionData, FDateTime::Now());

		FMetaHumanAssetDescription Asset;
		Asset.Name = FName(CharacterName);
		Asset.Details.PlatformsIncluded.Add(QualityLevel);
		FString JsonString;
		FJsonObjectConverter::UStructToJsonObjectString(Asset, JsonString);
		TStringConversion<TStringConvert<TCHAR, char>> ManifestConvert = StringCast<ANSICHAR>(*JsonString);
		const TConstArrayView<uint8> ManifestData(reinterpret_cast<const uint8*>(ManifestConvert.Get()), ManifestConvert.Length());
		ArchiveWriter.AddFile(TEXT("Manifest.json"), ManifestData, FDateTime::Now());
	}

	// Read Test Archive
	IFileHandle* ArchiveFile = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*TestArchiveFilename);
	FZipArchiveReader ArchiveReader(ArchiveFile);

	// Load
	FSourceMetaHuman ArchiveSource(&ArchiveReader);

	// Check
	CheckSourceMetaHuman(ArchiveSource, CharacterName, Version, bIsUefn, QualityLevel, SourceAssetsPath);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadSourceMetaHumanFromFiles, "MetaHumanSDK.MetaHumanTypesEditor.FSourceMetaHuman.FromFiles",
								EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoadSourceMetaHumanFromFiles::RunTest(const FString& Parameters)
{
	FString TestFilesRoot = FPaths::AutomationTransientDir() / TEXT("MetaHumanSDK");
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UAssets/46fwZzIM/Tier0/asset_ue"), false, EMetaHumanQualityLevel::Cinematic);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UAssets/af97ZzIm/Tier1/asset_ue"), false, EMetaHumanQualityLevel::High);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UAssets/f6ypZTTE/Tier2/asset_ue"), false, EMetaHumanQualityLevel::Medium);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UAssets/46fwZzIM/Tier3/asset_ue"), false, EMetaHumanQualityLevel::Low);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UEFNAssets/af97ZzIm/Tier0/asset_uefn"), true, EMetaHumanQualityLevel::High);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UEFNAssets/f6ypZTTE/Tier2/asset_uefn"), true, EMetaHumanQualityLevel::Medium);
	Test::GenerateAndLoadSourceMetaHumanFromFiles(TestFilesRoot / TEXT("UEFNAssets/46fwZzIM/Tier3/asset_uefn"), true, EMetaHumanQualityLevel::Low);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadSourceMetaHumanFromArchive, "MetaHumanSDK.MetaHumanTypesEditor.FSourceMetaHuman.FromArchive",
								EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLoadSourceMetaHumanFromArchive::RunTest(const FString& Parameters)
{
	FString TestFilesRoot = FPaths::AutomationTransientDir() / TEXT("MetaHumanSDK");
	Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, false, EMetaHumanQualityLevel::Cinematic);
	Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, false, EMetaHumanQualityLevel::High);
	Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, false, EMetaHumanQualityLevel::Medium);
	Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, false, EMetaHumanQualityLevel::Low);
	// Currently, .mhpkg files only support UE MetaHuman Assets
	// Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, true, EMetaHumanQualityLevel::High);
	// Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, true, EMetaHumanQualityLevel::Medium);
	// Test::GenerateAndLoadSourceMetaHumanFromArchive(TestFilesRoot, true, EMetaHumanQualityLevel::Low);

	return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
