// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeDnaModule.h"
#include "MetaHumanInterchangeDnaTranslator.h"

#include "InterchangeManager.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericMeshPipeline.h"

#include "SkelMeshDNAUtils.h"
#include "DNAToSkelMeshMap.h"
#include "SkelMeshDNAReader.h"
#include "DNAUtils.h"

#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetUserData.h"
#include "Engine/Engine.h"
#include "Animation/Skeleton.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/IAssetRegistry.h"

#define LOCTEXT_NAMESPACE "FDNAInterchangeModule"

void FInterchangeDnaModule::StartupModule()
{
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

		// Register DNA translator here. In this way we don't have to change Project Settings. 
		// Interchange manager will recognize DNA file extension and run the translator overriding existing DNA Factory.
		InterchangeManager.RegisterTranslator(UMetaHumanInterchangeDnaTranslator::StaticClass());
	};

	if (GEngine)
	{
		RegisterItems();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda(RegisterItems);
	}

	UInterchangeManager::SetInterchangeImportEnabled(true);
}

void FInterchangeDnaModule::ShutdownModule()
{
	UInterchangeManager::SetInterchangeImportEnabled(false);
}

FInterchangeDnaModule& FInterchangeDnaModule::GetModule()
{
	static const FName ModuleName = UE_MODULE_NAME;
	return FModuleManager::LoadModuleChecked<FInterchangeDnaModule>(ModuleName);
}

void FInterchangeDnaModule::SetSkelMeshDNAData(TNotNull<USkeletalMesh*> InSkelMesh, TSharedPtr<IDNAReader> InDNAReader)
{
	TObjectPtr<UDNAAsset> DNAAsset = NewObject<UDNAAsset>(InSkelMesh);
	DNAAsset->SetBehaviorReader(InDNAReader);
	DNAAsset->SetGeometryReader(InDNAReader);
	if (DNAAsset)
	{
		InSkelMesh->AddAssetUserData(DNAAsset);
	}
	
	const TSharedRef<FDNAToSkelMeshMap> FaceDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(InSkelMesh));
	FaceDnaToSkelMeshMap->MapJoints(InDNAReader.Get());
}

USkeletalMesh* FInterchangeDnaModule::ImportSync(const FString& InNewRigAssetName, const FString& InNewRigPath, TSharedPtr<IDNAReader> InDNAReader, TWeakObjectPtr<USkeleton> InSkeleton)
{
	USkeletalMesh* ImportedMesh = nullptr;

	if (InDNAReader)
	{
		// TODO: Since there is no support for memory stream yet in the Interchange SourceData system we need to create temporary file.
		FString DnaTempPath = FPaths::CreateTempFilename(
			FPlatformProcess::UserTempDir(),
			*InDNAReader->GetName(), TEXT(".dna"));

		WriteDNAToFile(InDNAReader.Get(), EDNADataLayer::All, DnaTempPath);

		if (FPaths::FileExists(DnaTempPath))
		{
			UE::Interchange::FScopedSourceData ScopedSourceData(DnaTempPath);

			FImportAssetParameters ImportAssetParameters;
			ImportAssetParameters.bIsAutomated = true;
			ImportAssetParameters.bFollowRedirectors = false;
			ImportAssetParameters.ReimportAsset = nullptr;
			ImportAssetParameters.bReplaceExisting = true;
			ImportAssetParameters.DestinationName = InNewRigAssetName;

			const FString PipelinePath = TEXT("/Interchange/Pipelines/DefaultAssetsPipeline");
			UInterchangeGenericAssetsPipeline* PipeAsset = LoadObject<UInterchangeGenericAssetsPipeline>(nullptr, *PipelinePath);
			PipeAsset->CommonMeshesProperties->bKeepSectionsSeparate = true;
			PipeAsset->CommonMeshesProperties->bImportLods = true;
			PipeAsset->CommonMeshesProperties->bUseFullPrecisionUVs = true;
			PipeAsset->CommonMeshesProperties->bRecomputeNormals = false;

			PipeAsset->MeshPipeline->bCreatePhysicsAsset = false;
			PipeAsset->MeshPipeline->MorphThresholdPosition = 0.00001f;
			// Default theshold for morph target deltas in Interchange mesh pipeline is 0.015 which is too low for DNA blend shapes.

			if (InSkeleton.IsValid())
			{
				PipeAsset->CommonSkeletalMeshesAndAnimationsProperties->Skeleton = InSkeleton;
				PipeAsset->CommonSkeletalMeshesAndAnimationsProperties->bAddCurveMetadataToSkeleton = false;
			}

			FSoftObjectPath AssetPipeline = FSoftObjectPath(PipeAsset);
			ImportAssetParameters.OverridePipelines.Add(AssetPipeline);
			UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

			UE::Interchange::FAssetImportResultRef ImportRes = InterchangeManager.ImportAssetWithResult(
				InNewRigPath, ScopedSourceData.GetSourceData(), ImportAssetParameters);

			for (UObject* Object : ImportRes->GetImportedObjects())
			{
				if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Object))
				{
					ImportedMesh = SkelMesh;
					break;
				}
			}

			// Delete temporary file.
			IFileManager::Get().Delete(*DnaTempPath);
		}
	}

	return ImportedMesh;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FInterchangeDnaModule, InterchangeDNA)
