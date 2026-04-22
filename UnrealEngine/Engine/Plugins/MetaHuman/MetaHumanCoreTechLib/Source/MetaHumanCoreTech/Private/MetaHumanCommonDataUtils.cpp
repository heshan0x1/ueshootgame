// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCommonDataUtils.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"

static constexpr const TCHAR* FaceDNAPath = TEXT("/ArchetypeDNA/SKM_Face.dna");
static constexpr const TCHAR* BodyDNAPath = TEXT("/ArchetypeDNA/SKM_Body.dna");
static constexpr const TCHAR* CombinedDNAPath = TEXT("/ArchetypeDNA/body_head_combined.dna");

static const FString& GetPluginContentDir()
{
	static const FString CachedContentDir = IPluginManager::Get().FindPlugin(TEXT(UE_PLUGIN_NAME))->GetContentDir();
	return CachedContentDir;
}

static FAssetData GetFirstAssetData(const FName& InPackageName)
{
	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	TArray<FAssetData> AnimBPData;
	AssetRegistry.GetAssetsByPackageName(InPackageName, AnimBPData);
	if (!AnimBPData.IsEmpty())
	{
		return AnimBPData[0];
	}

	return FAssetData();
}

const FString FMetaHumanCommonDataUtils::GetFaceDNAFilesystemPath()
{
	return GetPluginContentDir() + FaceDNAPath;
}

const FString FMetaHumanCommonDataUtils::GetBodyDNAFilesystemPath()
{
	return GetPluginContentDir() + BodyDNAPath;
}

const FString FMetaHumanCommonDataUtils::GetCombinedDNAFilesystemPath()
{
	return GetPluginContentDir() + CombinedDNAPath;
}

void FMetaHumanCommonDataUtils::SetPostProcessAnimBP(TNotNull<USkeletalMesh*> InSkelMesh, FStringView InAssetPath)
{
	FSoftObjectPath SoftPath(InAssetPath);
	const FName FaceRigLongPackageName = FName(*SoftPath.GetLongPackageName());
	
	FAssetData AnimBPAsset = GetFirstAssetData(FaceRigLongPackageName);
	if (AnimBPAsset.IsValid())
	{
		if (AnimBPAsset.IsInstanceOf(UAnimBlueprint::StaticClass()))
		{
			// UE editor is going through this route
			UAnimBlueprint* LoadedAnimBP = Cast<UAnimBlueprint>(AnimBPAsset.GetAsset());
			InSkelMesh->SetPostProcessAnimBlueprint(LoadedAnimBP->GetAnimBlueprintGeneratedClass());
		}
		else if (AnimBPAsset.IsInstanceOf(UAnimBlueprintGeneratedClass::StaticClass()))
		{
			// Cooked UEFN seems to be going via this route
			UAnimBlueprintGeneratedClass* LoadedAnimBP = Cast<UAnimBlueprintGeneratedClass>(AnimBPAsset.GetAsset());
			InSkelMesh->SetPostProcessAnimBlueprint(LoadedAnimBP);
		}
	}
	else
	{
		InSkelMesh->SetPostProcessAnimBlueprint(nullptr);
	}
}

TSoftObjectPtr<UObject> FMetaHumanCommonDataUtils::GetDefaultFaceControlRig(const FStringView InAssetPath)
{
	FSoftObjectPath SoftPath(InAssetPath);
	const FName FaceRigLongPackageName = FName(*SoftPath.GetLongPackageName());
	
	FAssetData FaceRigData = GetFirstAssetData(FaceRigLongPackageName);

	if (FaceRigData.IsValid())
	{
		return TSoftObjectPtr<UObject>(FaceRigData.ToSoftObjectPath());
	}

	return nullptr;
}

FStringView FMetaHumanCommonDataUtils::GetAnimatorPluginFaceSkeletonPath()
{
	return TEXTVIEW("/MetaHuman/IdentityTemplate/Face_Archetype_Skeleton.Face_Archetype_Skeleton");
}

FStringView FMetaHumanCommonDataUtils::GetAnimatorPluginFaceControlRigPath()
{
	return TEXTVIEW("/MetaHuman/IdentityTemplate/Face_ControlBoard_CtrlRig.Face_ControlBoard_CtrlRig");
}

FStringView FMetaHumanCommonDataUtils::GetAnimatorPluginFacePostProcessABPPath()
{
	return TEXTVIEW("/MetaHuman/IdentityTemplate/Face_PostProcess_AnimBP.Face_PostProcess_AnimBP");
}

FStringView FMetaHumanCommonDataUtils::GetCharacterPluginFaceSkeletonPath()
{
	return TEXTVIEW("/MetaHumanCharacter/Face/Face_Archetype_Skeleton.Face_Archetype_Skeleton");
}

FStringView FMetaHumanCommonDataUtils::GetCharacterPluginBodySkeletonPath()
{
	return TEXTVIEW("/MetaHumanCharacter/Female/Medium/NormalWeight/Body/metahuman_base_skel.metahuman_base_skel");
}

FStringView FMetaHumanCommonDataUtils::GetCharacterPluginFaceControlRigPath()
{
	return TEXTVIEW("/MetaHumanCharacter/Face/Face_ControlBoard_CtrlRig.Face_ControlBoard_CtrlRig");
}

FStringView FMetaHumanCommonDataUtils::GetCharacterPluginFacePostProcessABPPath()
{
	return TEXTVIEW("/MetaHumanCharacter/Face/ABP_Face_PostProcess.ABP_Face_PostProcess");
}

FStringView FMetaHumanCommonDataUtils::GetCharacterPluginBodyPostProcessABPPath()
{
	return TEXTVIEW("/MetaHumanCharacter/Body/ABP_Body_PostProcess.ABP_Body_PostProcess");
}

FString FMetaHumanCommonDataUtils::GetArchetypeDNAPath(const EMetaHumanImportDNAType InImportDNAType)
{
	FString DNAPath;

	switch (InImportDNAType)
	{
	case EMetaHumanImportDNAType::Face:
		DNAPath = GetFaceDNAFilesystemPath();
		break;
	case EMetaHumanImportDNAType::Body:
		DNAPath = GetBodyDNAFilesystemPath();
		break;
	case EMetaHumanImportDNAType::Combined:
		DNAPath = GetCombinedDNAFilesystemPath();
	default:
		break;
	}

	return DNAPath;
}
