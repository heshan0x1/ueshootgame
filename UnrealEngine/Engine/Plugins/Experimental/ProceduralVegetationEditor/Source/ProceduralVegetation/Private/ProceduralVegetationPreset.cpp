// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProceduralVegetationPreset.h"
#include "ProceduralVegetationModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVProfileFacade.h"
#include "Helpers/PVFoliageJSONHelper.h"
#include "Helpers/PVJSONHelper.h"
#include "Helpers/PVUtilities.h"
#include "Implementations/PVFoliage.h"

void FPVPresetVariationInfo::Fill(FName InName, FManagedArrayCollection Collection)
{
	Name = InName;
	
	const PV::Facades::FFoliageFacade Facade(Collection);
	for (int32 FoliageIndex = 0; FoliageIndex < Facade.NumFoliageInfo(); ++FoliageIndex)
	{
		FoliageMeshes.Emplace(Facade.GetFoliageInfo(FoliageIndex).Mesh);
	}

	const PV::Facades::FBranchFacade BranchFacade(Collection);
	Materials.Emplace(FSoftObjectPath(BranchFacade.GetTrunkMaterialPath()));

	if (const PV::Facades::FPlantProfileFacade PlantProfileFacade = PV::Facades::FPlantProfileFacade(Collection);
			PlantProfileFacade.NumProfileEntries() > 0)
	{
		for (int32 i = 0; i < PlantProfileFacade.NumProfileEntries(); i++)
		{
			PlantProfiles.Add(FString::FromInt(i));
		}
	}
}

UProceduralVegetationPreset::UProceduralVegetationPreset()
{
	FString AssetPath = FPackageName::GetLongPackagePath(GetPackage()->GetName());
	FoliageFolder.Path = FPaths::Combine(AssetPath, TEXT("Instances"));
	MaterialsFolder.Path = FPaths::Combine(AssetPath, TEXT("Materials"));
}

void UProceduralVegetationPreset::PostLoad()
{
	Super::PostLoad();

	if (PresetVariations.IsEmpty())
	{
		FillVariationInfo();
	}
}

void UProceduralVegetationPreset::LoadFromVariantFiles(const TMap<FString, FString>& InVariantFiles)
{
	for (const auto& [VariantName, VariantFile] : InVariantFiles)
	{
		UE_LOG(LogProceduralVegetation, Log, TEXT("Loaded variant : %s"), *VariantName);
		FManagedArrayCollection Collection;
		if (FString OutError; PV::LoadMegaPlantsJsonToCollection(Collection, VariantFile, OutError))
		{
			Variants.Add(VariantName, Collection);
		}
	}
}

void UProceduralVegetationPreset::UpdateDataAsset()
{
	UE_LOG(LogProceduralVegetation, Log, TEXT("UpdateDataAsset Clicked"));
	if (JsonDirectoryPath.Path.IsEmpty())
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("Please specify a JSON path"));
		return;
	}
	FString OutError;
	IFileManager& FileManager = IFileManager::Get();
	
	TArray<FString> FileNames;
	FileManager.FindFiles(FileNames, *JsonDirectoryPath.Path, TEXT("*.json"));

	Variants.Empty();

	FString JSONExtension = ".json";
	FString FoliageDataFileName = "FoliageData" + JSONExtension;

	if (!FileNames.Contains(FoliageDataFileName))
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("Unable to find Foliage Data file"));
		return;
	}
	else
	{
		FileNames.Remove(FoliageDataFileName);
	}

	FString FoliageDataPath = JsonDirectoryPath.Path / FoliageDataFileName;

	PV::FoliageVariationsMap FoliageVariationsMap;

	if (!PV::PVFoliageJSONHelper::LoadFoliageData(FoliageDataPath, FoliageFolder.Path, FoliageVariationsMap, OutError))
	{
		UE_LOG(LogProceduralVegetation, Warning, TEXT("Unable to load Foliage Data :  Error : %s"), *OutError);
	}
	
	TArray<TSharedPtr<FJsonObject>> MetaFiles;
	for (const auto& FileName : FileNames)
	{
		FString FullPath = JsonDirectoryPath.Path / FileName;
		FManagedArrayCollection Collection;
		

		if ( PV::LoadMegaPlantsJsonToCollection(Collection, FullPath, OutError))
		{
			UE_LOG(LogProceduralVegetation, Log, TEXT("Variant %s loaded from file"), *FPaths::GetBaseFilename(FullPath));
			Variants.Add(FPaths::GetBaseFilename(FullPath), Collection);
		}
		else if (TSharedPtr<FJsonObject> MetaJson = PV::LoadMetaFileIntoJsonObject(FullPath, OutError))
		{
			MetaFiles.Add(MetaJson);
		}
		if (!OutError.IsEmpty())
		{
			UE_LOG(LogProceduralVegetation, Warning, TEXT("%s"), *OutError);
		}
	}
	
	int VariantIndex = 0;
	for (auto& Pair : Variants)
	{
		if (MetaFiles.IsValidIndex(VariantIndex))
		{
			PV::LoadMetaJsonToCollection(Pair.Value, MetaFiles[VariantIndex]);
		}
		else if (MetaFiles.Num() > 0 && VariantIndex > MetaFiles.Num() - 1)
		{
			PV::LoadMetaJsonToCollection(Pair.Value, MetaFiles[0]);
		}

		// load Foliage Data again
		if (FoliageVariationsMap.Contains(Pair.Key))
		{
			FString VariationPresetPath = JsonDirectoryPath.Path / Pair.Key + JSONExtension;
			
			if (!PV::PVFoliageJSONHelper::LoadFoliageDataInCollection(Pair.Value, VariationPresetPath, FoliageVariationsMap[Pair.Key], OutError))
			{
				UE_LOG(LogProceduralVegetation, Warning, TEXT("Unable to load foliage data in collection:  Error : %s"), *OutError);
			}
		}
		
		VariantIndex++;
	}
	
	if (bCreateProfileDataAsset)
	{
		CreateProfileDataAsset();
	}
	
	UpdateFoliageAndMaterialPath();
	FillVariationInfo();
	GetPackage()->SetDirtyFlag(true);
}

void UProceduralVegetationPreset::FillVariationInfo()
{
	PresetVariations.Empty();
	for (const auto& [VariantName, VariantData] : Variants)
	{
		FPVPresetVariationInfo Info;
		Info.Fill(FName(VariantName), VariantData);
		PresetVariations.Emplace(Info);
	}
}

void UProceduralVegetationPreset::ShowHideInternalProperties(bool bDebugEnable)
{
#if WITH_METADATA
	FString CategoryName(TEXT("Preset Data"));
	if (!bDebugEnable)
	{
		CategoryName = TEXT("Internal");
	}

	for (TFieldIterator<FProperty> It(StaticClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (Property && Property->HasMetaData(TEXT("DevelopmentOnly")))
		{
			Property->SetMetaData(TEXT("Category"), *CategoryName);
		}
	}

	for (TFieldIterator<UFunction> It(StaticClass()); It; ++It)
	{
		UFunction* Function = *It;
		if (Function && Function->HasMetaData(TEXT("DevelopmentOnly")))
		{
			Function->SetMetaData(TEXT("Category"), *CategoryName);
		}
	}
#endif
}

void UProceduralVegetationPreset::CreateProfileDataAsset()
{
	FString ProfilePackageName(PlantProfileName);
	FString PackagePath = FPaths::Combine(FPackageName::GetLongPackagePath(GetPackage()->GetName()), ProfilePackageName);
	UPackage* PlantProfilePackage = CreatePackage(*PackagePath);
	
	UPlantProfileAsset* NewPlantProfileAsset = NewObject<UPlantProfileAsset>(PlantProfilePackage, *ProfilePackageName, RF_Public | RF_Standalone);

	for (auto& Pair : Variants)
	{
		PV::Facades::FPlantProfileFacade Facade = PV::Facades::FPlantProfileFacade(Pair.Value);
		for (int32 Index = 0; Index < Facade.NumProfileEntries(); Index++)
		{
			FPlantProfile Profile;
			Profile.ProfileName = FString::Format(TEXT("Profile_{0}"), {Index});
			auto Points = Facade.GetProfilePoints(Index);
			Profile.ProfilePoints = Points;
			NewPlantProfileAsset->Profiles.Add(Profile);
		}
	}
	
	IAssetRegistry::Get()->AssetCreated(NewPlantProfileAsset);
	PlantProfilePackage->SetDirtyFlag(true);
}

void UProceduralVegetationPreset::UpdateFoliageAndMaterialPath()
{
	for (auto& Pair : Variants)
	{
		PV::Facades::FFoliageFacade FoliageFacade(Pair.Value);
		PV::Facades::FBranchFacade BranchFacade(Pair.Value);
			
		for (int32 FoliageIndex = 0; FoliageIndex < FoliageFacade.NumFoliageInfo(); FoliageIndex++)
		{
			if (!TrunkMaterialName.IsEmpty())
			{
				FString FullMaterialName = FPaths::Combine(*MaterialsFolder.Path, TrunkMaterialName + '.' + TrunkMaterialName);
				BranchFacade.SetTrunkMaterialPath( FullMaterialName );
			}
			else
			{
				UE_LOG(LogProceduralVegetation, Warning, TEXT("Material name is empty"));
			}
		}
	}
}
