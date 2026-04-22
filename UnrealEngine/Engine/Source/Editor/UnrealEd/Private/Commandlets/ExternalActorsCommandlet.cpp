// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/ExternalActorsCommandlet.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PackageHelperFunctions.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ExternalActorsCommandlet)

DEFINE_LOG_CATEGORY_STATIC(LogExternalActorsCommandlet, Log, All);

UExternalActorsCommandlet::UExternalActorsCommandlet(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

UWorld* UExternalActorsCommandlet::LoadWorld(const FString& LevelToLoad)
{
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogExternalActorsCommandlet, Log, TEXT("Loading level %s."), *LevelToLoad);
	CLEAR_WARN_COLOR();

	UPackage* MapPackage = LoadPackage(nullptr, *LevelToLoad, LOAD_None);
	if (!MapPackage)
	{
		UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Error loading %s."), *LevelToLoad);
		return nullptr;
	}

	return UWorld::FindWorldInPackage(MapPackage);
}

int32 UExternalActorsCommandlet::Main(const FString& Params)
{
	FPackageSourceControlHelper PackageHelper;

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	ApplyCommandLineSwitches(this, Switches);

	if (bDisable)
	{
		if (bRepair)
		{
			UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Bad parameters: -repair cannot be used with -disable"));
			return 1;
		}

		bListMaps |= !DumpCSVFile.IsEmpty();
	}
	else if (bForce || bReport || bListMaps || DumpCSVFile.Len())
	{
		UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Bad parameters: -force, -report, -listmaps and -dumpcsv can only be used along with -disable"));
		return 1;
	}

	if (bDisable && bListMaps)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName);

		UE_LOG(LogExternalActorsCommandlet, Display, TEXT("Waiting for asset registry..."));
		AssetRegistryModule.Get().SearchAllAssets(true);

		TArray<FAssetData> WorldAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), WorldAssets, true);
		WorldAssets.Sort();

		struct FWorldEntry
		{
			FString Path;
			int32 NumExternalActors;
			FString DateModified;

			inline bool operator<(const FWorldEntry& Other) const
			{
				return NumExternalActors > Other.NumExternalActors;
			}
		};

		TArray<FWorldEntry> WorldEntries;
		for (const FAssetData& WorldAsset : WorldAssets)
		{
			if (ULevel::GetIsLevelUsingExternalActorsFromAsset(WorldAsset))
			{
				const FString ExternalActorsPath = ULevel::GetExternalActorsPath(WorldAsset.PackageName.ToString());

				TArray<FAssetData> WorldExternalActors;
				AssetRegistryModule.Get().GetAssetsByPath(*ExternalActorsPath, WorldExternalActors, true, true);

				if (!ULevel::GetIsLevelPartitionedFromAsset(WorldAsset) || ULevel::GetIsStreamingDisabledFromAsset(WorldAsset))
				{
					FString DateModified;
					WorldAsset.GetTagValue(TEXT("DateModified"), DateModified);

					WorldEntries.Add(
					{
						.Path = WorldAsset.PackageName.ToString(),
						.NumExternalActors = WorldExternalActors.Num(),
						.DateModified = DateModified
					});

					UE_LOG(LogExternalActorsCommandlet, Display, TEXT("Level '%s' is a potential candidate to have external actors disabled (%d)."), *WorldAsset.GetSoftObjectPath().ToString(), WorldExternalActors.Num());
				}
				else
				{
					UE_LOG(LogExternalActorsCommandlet, Verbose, TEXT("Level '%s' is not a potential candidate to have external actors disabled (%d)."), *WorldAsset.GetSoftObjectPath().ToString(), WorldExternalActors.Num());
				}
			}
		}

		if (DumpCSVFile.Len())
		{
			FArchive* LogFile = IFileManager::Get().CreateFileWriter(*DumpCSVFile);
			if (!LogFile)
			{
				UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Cannot create csv file '%s'"), *DumpCSVFile);
				return 1;
			}

			FString LineEntry = TEXT("Map,NumOFPA,DateModified") LINE_TERMINATOR;
			LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());

			WorldEntries.Sort();

			for (const FWorldEntry& WorldEntry : WorldEntries)
			{
				LineEntry = FString::Printf(TEXT("%s,%d,%s" LINE_TERMINATOR), *WorldEntry.Path, WorldEntry. NumExternalActors, *WorldEntry.DateModified);
				LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
			}

			LogFile->Close();
			delete LogFile;
		}

		return 0;
	}

	TArray<FString> MapList;

	if (!MapListFile.IsEmpty())
	{
		if (!FFileHelper::LoadFileToStringArray(MapList, *MapListFile))
		{
			UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Invalid map list filename"));
			return 1;
		}
	}
	else
	{
		if (Tokens.Num() < 1)
		{
			UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Missing map name"));
			return 1;
		}

		MapList.Add(Tokens[0]);
	}

	int32 Result = 1;
	for (const FString& MapName : MapList)
	{
		FString FullMapName;
		if (!FPackageName::SearchForPackageOnDisk(MapName, &FullMapName))
		{
			UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Unknown level '%s'"), *MapName);
			Result = 0;
			continue;
		}

		// Load world
		UWorld* MainWorld = LoadWorld(FullMapName);
		if (!MainWorld)
		{
			UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Unknown world '%s'"), *FullMapName);
			Result = 0;
			continue;
		}

		TSet<UPackage*> PackagesToSave;
		TArray<FString> PackagesToDelete;	

		if (bDisable)
		{
			if (!MainWorld->PersistentLevel->IsUsingExternalActors())
			{
				UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Cannot disable external actors for partitioned level '%s' (already disabled)"), *FullMapName);
				Result = 0;
				continue;
			}

			if (UWorldPartition* WorldPartition = MainWorld->GetWorldPartition())
			{
				int32 NumErrors = 0;

				if (WorldPartition->IsStreamingEnabled())
				{
					UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Cannot disable external actors for partitioned level '%s' with streaming enabled"), *FullMapName);
					NumErrors++;

					if (bForce)
					{
						WorldPartition->SetEnableStreaming(false);
					}
				}

				if (UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager())
				{
					if (DataLayerManager->GetDataLayerInstances().Num())
					{
						UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Cannot disable external actors for partitioned level '%s' with data layers"), *FullMapName);
						NumErrors++;
					}
				}

				if (!NumErrors)
				{
					UE_LOG(LogExternalActorsCommandlet, Display, TEXT("External actors for partitioned level '%s' can be disabled"), *FullMapName);
				}
				else if (bForce)
				{
					UE_LOG(LogExternalActorsCommandlet, Display, TEXT("External actors for partitioned level '%s' will be forcibly disabled"), *FullMapName);
				}
				else
				{
					Result = 0;
					continue;
				}			

				if (!bReport)
				{
					WorldPartition->Initialize(MainWorld, FTransform::Identity);

					if (!UWorldPartition::RemoveWorldPartition(WorldPartition->GetWorld()->GetWorldSettings()))
					{
						UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Error disabling external actors for partitioned level '%s'"), *FullMapName);
						Result = 0;
						continue;
					}

					WorldPartition->Uninitialize();
				}
			}

			if (!bReport)
			{
				for (AActor* Actor : MainWorld->PersistentLevel->Actors)
				{
					if (IsValid(Actor) && Actor->IsMainPackageActor())
					{
						PackagesToDelete.Add(Actor->GetPackage()->GetLoadedPath().GetLocalFullPath());
						Actor->SetPackageExternal(false);
					}
				}

				if (MainWorld->PersistentLevel->IsUsingExternalObjects())
				{
					ForEachObjectWithOuter(MainWorld->PersistentLevel, [MainWorld, &PackagesToDelete](UObject* Object)
					{
						if (Object->IsPackageExternal() && !Object->IsA<AActor>())
						{
							PackagesToDelete.Add(Object->GetPackage()->GetLoadedPath().GetLocalFullPath());
							FExternalPackageHelper::SetPackagingMode(Object, MainWorld->PersistentLevel, false);
						}
						return true;
					}, true, RF_MirroredGarbage);
				}

				if (PackagesToDelete.Num())
				{
					PackagesToSave.Add(MainWorld->GetPackage());
				}

				MainWorld->PersistentLevel->SetUseActorFolders(false);
				MainWorld->PersistentLevel->SetUseExternalActors(false);
			}
		}
		else
		{
			// Validate external actors
			FString ExternalActorsPath = ULevel::GetExternalActorsPath(FullMapName);
			FString ExternalActorsFilePath = FPackageName::LongPackageNameToFilename(ExternalActorsPath);

			// Look for duplicated actor GUIDs
			TMap<FGuid, AActor*> ActorGuids;

			if (IFileManager::Get().DirectoryExists(*ExternalActorsFilePath))
			{
				bool bResult = IFileManager::Get().IterateDirectoryRecursively(*ExternalActorsFilePath, [this, &PackagesToSave, &PackagesToDelete, &ActorGuids](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
				{
					if (!bIsDirectory)
					{
						FString Filename(FilenameOrDirectory);
						if (Filename.EndsWith(FPackageName::GetAssetPackageExtension()))
						{
							AActor* MainPackageActor = nullptr;
							AActor* PotentialMainPackageActor = nullptr;

							const FString PackageName = FPackageName::FilenameToLongPackageName(*Filename);
							if (UPackage* Package = LoadPackage(nullptr, *Filename, LOAD_None, nullptr, nullptr))
							{
								ForEachObjectWithPackage(Package, [&MainPackageActor, &PotentialMainPackageActor](UObject* Object)
								{
									if (AActor* Actor = Cast<AActor>(Object))
									{
										if (Actor->IsMainPackageActor())
										{
											MainPackageActor = Actor;
											PotentialMainPackageActor = nullptr;
										}
										else if (!MainPackageActor)
										{
											if (!Actor->IsChildActor())
											{
												PotentialMainPackageActor = Actor;
											}
										}
									}
									return true;
								});
							}

							if (!MainPackageActor)
							{
								UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Missing main actor for file '%s'"), *Filename);

								if (bRepair)
								{
									if (PotentialMainPackageActor)
									{
										PotentialMainPackageActor->SetPackageExternal(false);
										PotentialMainPackageActor->SetPackageExternal(true);
								
										UPackage* PackageToSave = PotentialMainPackageActor->GetPackage();
										PackagesToSave.Add(PackageToSave);

										MainPackageActor = PotentialMainPackageActor;
									}

									PackagesToDelete.Add(Filename);
								}
							}

							if (MainPackageActor)
							{
								if (ActorGuids.Contains(MainPackageActor->GetActorGuid()))
								{
									UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Duplicated actor guid for file '%s'"), *Filename);

									if (bRepair)
									{
										FSetActorGuid SetActorGuid(MainPackageActor, FGuid::NewGuid());

										UPackage* PackageToSave = MainPackageActor->GetPackage();
										PackagesToSave.Add(PackageToSave);
									}
								}
								else
								{
									ActorGuids.Add(MainPackageActor->GetActorGuid(), MainPackageActor);
								}
							}
						}
						else
						{
							UE_LOG(LogExternalActorsCommandlet, Error, TEXT("Invalid actor file '%s'"), *Filename);

							if (bRepair)
							{
								PackagesToDelete.Add(Filename);
							}
						}
					}
					return true;
				});
			}
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;

		for (UPackage* PackageToSave : PackagesToSave)
		{
			const FString PackageFileName = SourceControlHelpers::PackageFilename(PackageToSave);
			UE_LOG(LogExternalActorsCommandlet, Display, TEXT("Saving package '%s'"), *PackageFileName);

			if (PackageHelper.Checkout(PackageToSave))
			{				
				if (UPackage::SavePackage(PackageToSave, nullptr, *PackageFileName, SaveArgs))
				{
					PackageHelper.AddToSourceControl(PackageToSave);
				}
			}
		}

		CollectGarbage(RF_NoFlags);

		PackagesToDelete.Sort();
		for (const FString& PackageToDelete : PackagesToDelete)
		{
			const FString PackageFileName = SourceControlHelpers::PackageFilename(PackageToDelete);
			UE_LOG(LogExternalActorsCommandlet, Display, TEXT("Deleting package '%s'"), *PackageFileName);

			PackageHelper.Delete(*PackageToDelete);
		}
	}

	return Result;
}
