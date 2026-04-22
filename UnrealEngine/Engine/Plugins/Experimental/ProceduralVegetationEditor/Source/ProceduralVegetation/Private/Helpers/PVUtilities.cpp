// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PVUtilities.h"
#include "ProceduralVegetationModule.h"
#include "ProceduralVegetationPreset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Nodes/PVImporterSettings.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetData.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVPointFacade.h"
#include "Implementations/PVFoliage.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace PV::Utilities
{
#if WITH_EDITOR
	static TAutoConsoleVariable<bool> CVarEnablePVDebugMode(
		TEXT("PV.DebugMode.Enabled"),
		true,
		TEXT("Enables debug mode for the Procedural Vegetation Editor"), 
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* ConsoleVariable)
			{
				UPVImporterSettings::StaticClass()->GetDefaultObject<UPVImporterSettings>()->bExposeToLibrary = ConsoleVariable->GetBool();
				UProceduralVegetationPreset::ShowHideInternalProperties(ConsoleVariable->GetBool());
			})
	);
#endif

	bool DebugModeEnabled()
	{
#if WITH_EDITOR
		return CVarEnablePVDebugMode.GetValueOnAnyThread();
#else
		return false;
#endif
	}

	void AddFoliageInstancesToActor(const Facades::FFoliageFacade& FoliageFacade, AActor* Actor,
	                                TMap<FString, TObjectPtr<UMeshComponent>>& OutInstancedComponentMap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PV::Utilities::AddFoliageInstancesToActor());

		auto OnComponentCreated = [&Actor](UMeshComponent* InInstancedComponent)
			{
				InInstancedComponent->RegisterComponentWithWorld(Actor->GetWorld());
				InInstancedComponent->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				Actor->AddInstanceComponent(InInstancedComponent);
			};
		
		AddFoliageInstances(FoliageFacade, Actor, FFoliageComponentCreatedCallback::CreateLambda(OnComponentCreated),OutInstancedComponentMap);
	}

	void AddFoliageInstances(const Facades::FFoliageFacade& InFoliageFacade, const UObject* InParent, FFoliageComponentCreatedCallback InCallback, TMap<FString, TObjectPtr<UMeshComponent>>& OutInstancedComponentMap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PV::Utilities::AddFoliageInstances());

		int32 NumInstances = InFoliageFacade.NumFoliageEntries();

		OutInstancedComponentMap.Empty();

		if (NumInstances > 0)
		{
			for (int32 FoliageNameIndex = 0; FoliageNameIndex < InFoliageFacade.NumFoliageInfo(); ++FoliageNameIndex)
			{
				FPVFoliageInfo FoliageInfo = InFoliageFacade.GetFoliageInfo(FoliageNameIndex);
				FString FoliageName = FoliageInfo.Mesh.ToString();
				FSoftObjectPath MeshPath(FoliageName);

				UStaticMesh* StaticFoliageMesh = PackageExists(MeshPath.GetLongPackageName(), UStaticMesh::StaticClass()) ? LoadObject<UStaticMesh>(nullptr, *FoliageName) : nullptr;
				USkeletalMesh* SkeletalFoliageMesh = PackageExists(MeshPath.GetLongPackageName(), USkeletalMesh::StaticClass()) ? LoadObject<USkeletalMesh>(nullptr, *FoliageName) : nullptr;

				if(StaticFoliageMesh)
				{
					UInstancedStaticMeshComponent* InstancedComponent = NewObject<UInstancedStaticMeshComponent>(InParent != nullptr ? InParent->GetPackage() : GetTransientPackage(), NAME_None);
					InstancedComponent->SetStaticMesh(StaticFoliageMesh);
					OutInstancedComponentMap.Add(FoliageName, InstancedComponent);
					InCallback.ExecuteIfBound(InstancedComponent);
				}
				else if(SkeletalFoliageMesh)
				{
#if WITH_EDITORONLY_DATA
					if (!SkeletalFoliageMesh->NaniteSettings.bEnabled)
					{
						SkeletalFoliageMesh->NaniteSettings.bEnabled = true;
						SkeletalFoliageMesh->Build();
						SkeletalFoliageMesh->MarkPackageDirty();
						UE_LOG(LogProceduralVegetation, Warning, TEXT("Enabling Nanite for the Skeletal Mesh {%s}, its required for InstancedSkinnedMeshComponent"), *FoliageName);
					}
#else
					UE_LOG(LogProceduralVegetation, Warning, TEXT("Instanced Skinned Mesh component only works with nanite meshes make sure nanite is enabled for mesh {%s}"), *FoliageName);
#endif
					
					UInstancedSkinnedMeshComponent* InstancedComponent = NewObject<UInstancedSkinnedMeshComponent>(InParent != nullptr ? InParent->GetPackage() : GetTransientPackage(), NAME_None);
					InstancedComponent->SetSkinnedAssetAndUpdate(SkeletalFoliageMesh);
					OutInstancedComponentMap.Add(FoliageName, InstancedComponent);
					InCallback.ExecuteIfBound(InstancedComponent);
				}
			}

			TMap<FString, TArray<FTransform>> InstanceTransformsMap;

			for (int32 Id = 0; Id < NumInstances; Id++)
			{
				Facades::FFoliageEntryData Data = InFoliageFacade.GetFoliageEntry(Id);
				FPVFoliageInfo FoliageInfo = InFoliageFacade.GetFoliageInfo(Data.NameId);
				FString FoliageName = FoliageInfo.Mesh.ToString();
				
				const FTransform Transform = InFoliageFacade.GetFoliageTransform(Id);

				InstanceTransformsMap.FindOrAdd(FoliageName).Add(Transform);
			}
		
			for(auto FoliageEntry : InstanceTransformsMap)
			{
				if (FoliageEntry.Value.Num() > 0)
				{
					if (auto InstancedComponent = OutInstancedComponentMap.Find(FoliageEntry.Key))
					{
						if(TObjectPtr<UInstancedStaticMeshComponent> StaticMeshInstancedComponent = Cast<UInstancedStaticMeshComponent>(*InstancedComponent))
						{
							StaticMeshInstancedComponent->AddInstances(FoliageEntry.Value, false);	
						}
						else if(TObjectPtr<UInstancedSkinnedMeshComponent> SkinnedInstancedComponent = Cast<UInstancedSkinnedMeshComponent>(*InstancedComponent))
						{
							TArray<int32> BankIndices;
							BankIndices.AddUninitialized(FoliageEntry.Value.Num());
							SkinnedInstancedComponent->AddInstances(FoliageEntry.Value, BankIndices, false);
						}
					}
				}
			}
		}
	}

	FString FormatLongPackageNameErrorCode(const FPackageName::EErrorCode ErrorCode)
	{
		switch (ErrorCode)
		{
		case FPackageName::EErrorCode::PackageNameUnknown:
			return "Unknown Export Path error";
		case FPackageName::EErrorCode::PackageNameEmptyPath:
			return "Empty Export Path";
		case FPackageName::EErrorCode::PackageNamePathNotMounted:
			return "Mount Point for Export Path is not valid";
		case FPackageName::EErrorCode::PackageNamePathIsMemoryOnly:
			return "Export Path exists in memory only. ";
		case FPackageName::EErrorCode::PackageNameSpacesNotAllowed:
			return "Spaces are not allowed in Export Path";	
		case FPackageName::EErrorCode::PackageNameContainsInvalidCharacters:
			return FString::Printf(TEXT("Export Path contains one of \n%s\n Invalid Characters"), TEXT(R"(\\:*?\"<>|' ,.&!~\n\r\t@#)"));
		case FPackageName::EErrorCode::LongPackageNames_PathTooShort:
			return "Export Path is too small to be a valid path";
		case FPackageName::EErrorCode::LongPackageNames_PathWithNoStartingSlash:
			return "Export Path has to start with a slash [/]";
		case FPackageName::EErrorCode::LongPackageNames_PathWithTrailingSlash:
			return "Export Path cannot end with a slash [/]";
		case FPackageName::EErrorCode::LongPackageNames_PathWithDoubleSlash:
			return "Export Path contains a double slash [/]";
		}
		return FString();
	}

	bool ValidateAssetPathAndName(const FString& AssetName, const FString& Path, UClass* InClass, FString& OutError)
	{
		if (FPackageName::EErrorCode ErrorCode; !FPackageName::IsValidLongPackageName(Path, false, &ErrorCode))
        {
			OutError =  FormatLongPackageNameErrorCode(ErrorCode);
        	return false;
        }

		if (FText Reason; !IsFileNameValid(FName(AssetName), Reason))
        {
			OutError = Reason.ToString();
        	return false;
        }

		return true;
	}

	bool IsFileNameValid(FName FileName, FText& Reason)
	{
		FileName.IsValidObjectName(Reason);
		FFileHelper::IsFilenameValidForSaving(FileName.ToString(), Reason);

		return Reason.IsEmpty();
	}

	bool DoesConflictingPackageExist(const FString& LongPackageName, UClass* AssetClass)
	{
		UPackage* Package = FindPackage(nullptr, *LongPackageName);
		if (IsValid(Package))
		{
			UObject* ExistingAsset = Package->FindAssetInPackage();
			return !ExistingAsset || ExistingAsset->GetClass() != AssetClass;
		}
		else
		{
			TArray<FAssetData> OutAssets;
			IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
			AssetRegistry.GetAssetsByPackageName(*LongPackageName, OutAssets, true);

			if (OutAssets.Num() == 0)
			{
				return false;
			}

			if (OutAssets.Num() != 1
				|| OutAssets[0].AssetClassPath != AssetClass->GetClassPathName())
			{
				return true;
			}
		}

		return false;
	}

	bool PackageExists(const FString& LongPackageName, UClass* AssetClass)
	{
		UPackage* Package = FindPackage(nullptr, *LongPackageName);
		if (IsValid(Package))
		{
			UObject* ExistingAsset = Package->FindAssetInPackage();
			return ExistingAsset && ExistingAsset->GetClass() == AssetClass;
		}
		else
		{
			TArray<FAssetData> OutAssets;
			IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
			AssetRegistry.GetAssetsByPackageName(*LongPackageName, OutAssets, true);

			if (OutAssets.Num() == 0)
			{
				return false;
			}

			if (OutAssets[0].AssetClassPath == AssetClass->GetClassPathName())
			{
				return true;
			}
		}

		return false;
	}

	int32 GetMeshTriangles(const FString InMeshPath)
	{
		UStaticMesh* StaticFoliageMesh = LoadObject<UStaticMesh>(nullptr, *InMeshPath);
		USkeletalMesh* SkeletalFoliageMesh = LoadObject<USkeletalMesh>(nullptr, *InMeshPath);

		int32 NumTriangles = 0;
		
		if (StaticFoliageMesh)
		{
			NumTriangles = StaticFoliageMesh->GetNumTriangles(0);
		}
		else if (SkeletalFoliageMesh)
		{
			FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalFoliageMesh->GetResourceForRendering();
			if (SkelMeshRenderData && SkelMeshRenderData->LODRenderData.Num() > 0)
			{
				const FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[0];
				NumTriangles = LODData.GetTotalFaces();
			}

			return NumTriangles;
		}

		return NumTriangles;
	}

	bool IsValidPVData(const FManagedArrayCollection& Collection)
	{
		Facades::FPointFacade PointFacade = Facades::FPointFacade(Collection);
		Facades::FBranchFacade BranchFacade = Facades::FBranchFacade(Collection);
	
		return PointFacade.IsValid() && BranchFacade.IsValid();
	}
}
