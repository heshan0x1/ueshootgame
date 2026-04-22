// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsCommandlets.h"
#include "PlainPropsEngineBindings.h"
#include "PlainPropsRoundtripTest.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AsciiSet.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"

UTestPlainPropsCommandlet::UTestPlainPropsCommandlet(const FObjectInitializer& Init)
: Super(Init)
{}

int32 UTestPlainPropsCommandlet::Main(const FString& Params)
{
	using namespace PlainProps;
	using namespace PlainProps::UE;

	TArray<UObject*> Objects;
	if (int32 LoadIdx = Params.Find("-load="); LoadIdx != INDEX_NONE)
	{
		// E.g.
		// -run=TestPlainProps -load=/BRRoot/BRRoot.BRRoot,/Game/Maps/FrontEnd.FrontEnd
		// -run=TestPlainProps -load=/Game/Maps/Highrise.Highrise -save

		const TCHAR* It = &Params[LoadIdx + 6];
		FStringView Assets(It, FAsciiSet::FindFirstOrEnd(It, FAsciiSet(" ")) - It);
		UE_LOGFMT(LogPlainPropsEngine, Display, "Loading {Assets}...", Assets);

		int32 CommaIndex;
		while (Assets.FindChar(',', CommaIndex))
		{
			FSoftObjectPath AssetPath(Assets.Left(CommaIndex)); 
			UObject* Object = AssetPath.TryLoad();
			check(Object);
			Objects.Add(Object);
			Assets.RightChopInline(CommaIndex + 1);
		}

		FSoftObjectPath AssetPath(Assets);
		UObject* Object = AssetPath.TryLoad();
		check(Object);
		Objects.Add(Object);
	}
	else if (Params.Find("-loadmaps") != INDEX_NONE)
	{
		// load all .umaps in asset registry
		UE_LOGFMT(LogPlainPropsEngine, Display, "Loading asset registry...");
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);
		AssetRegistryModule.Get().SearchAllAssets(true);

		UE_LOGFMT(LogPlainPropsEngine, Display, "Gathering all maps...");
		TArray<FAssetData> Maps;
		AssetRegistryModule.Get().GetAssetsByClass(FTopLevelAssetPath("/Script/Engine", "World"), /* out */ Maps, true);

		UE_LOGFMT(LogPlainPropsEngine, Display, "Loading all {Maps} maps...", Maps.Num());
		for (FAssetData& Map : Maps)
		{
			FSoftObjectPath AssetPath = Map.GetSoftObjectPath();
			AssetPath.LoadAsync({});
		}
	}

	FlushAsyncLoading();
	UE_LOGFMT(LogPlainPropsEngine, Display, "Starting test...");

	UE_LOGFMT(LogPlainPropsEngine, Display, "Binding types to PlainProps schemas...");
	
	EBindMode Mode = EBindMode::All;
	if (Params.Find("-bind=source") != INDEX_NONE)
	{
		Mode = EBindMode::Source;
	}
	else if (Params.Find("-bind=runtime") != INDEX_NONE)
	{
		Mode = EBindMode::Runtime;
	}

	SchemaBindAllTypes(Mode);
	CustomBindEngineTypes(Mode);

	// Parse roundtrip options
	ERoundtrip Options = ERoundtrip::PP | ERoundtrip::UPS | ERoundtrip::TPS;
	if (Params.Find("-pp") != INDEX_NONE)
	{
		Options = ERoundtrip::PP | ERoundtrip::TextMemory;
	}
	else if (Params.Find("-text") != INDEX_NONE)
	{
		Options = ERoundtrip::TextMemory | ERoundtrip::TextStable;
	}

	// Roundtrip
	if (Params.Find("-package") != INDEX_NONE)
	{
		RoundtripViaPackages(Objects, Options);
	}
	else
	{ 
		RoundtripViaBatch(Objects, Options);
	}
	
	FPlatformMisc::RequestExit(true, TEXT("TestPlainProps"));
	UE_LOGFMT(LogPlainPropsEngine, Display, "Done!");
	return 0;
}
