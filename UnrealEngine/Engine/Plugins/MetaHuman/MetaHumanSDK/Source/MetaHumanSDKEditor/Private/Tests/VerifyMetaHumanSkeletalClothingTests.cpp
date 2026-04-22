// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MetaHumanAssetReport.h"
#include "MetaHumanSDKEditor.h"
#include "Verification/VerifyMetaHumanSkeletalClothing.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Logging/StructuredLog.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UE::MetaHuman::Test
{
UMetaHumanAssetReport* GenerateReport()
{
	UMetaHumanAssetReport* Report = NewObject<UMetaHumanAssetReport>();
	Report->SetSilent(true);
	return Report;
}

USkeletalMesh* GenerateRootObject()
{
	UPackage* TestSkelMeshPackage = NewObject<UPackage>(nullptr, TEXT("/Test/Clothing/TestClothingItem/TestClothingItem"));
	USkeletalMesh* SkelMesh(NewObject<USkeletalMesh>(TestSkelMeshPackage, FName(TEXT("TestClothingItem")), RF_Public | RF_Standalone));
	// Add path to dependent assets so they can be found by the Asset Registry while in-memory
	IAssetRegistry::Get()->AddPath(TEXT("/Test/Clothing/TestClothingItem/TestClothingItem"));
	return SkelMesh;
}

USkeletalMesh* GenerateSkelMesh(FString Name)
{
	UPackage* TestSkelMeshPackage = NewObject<UPackage>(nullptr, *(FString(TEXT("/Test/Clothing/TestClothingItem/TestClothingItem")) / Name));
	USkeletalMesh* SkelMesh(NewObject<USkeletalMesh>(TestSkelMeshPackage, FName(Name), RF_Public | RF_Standalone));
	return SkelMesh;
}

UStaticMesh* GenerateStaticMesh(FString Name)
{
	UPackage* TestSkelMeshPackage = NewObject<UPackage>(nullptr, *(FString(TEXT("/Test/Clothing/TestClothingItem/TestClothingItem")) / Name));
	UStaticMesh* StaticMesh(NewObject<UStaticMesh>(TestSkelMeshPackage, FName(Name), RF_Public | RF_Standalone));
	return StaticMesh;
}

UMaterialInterface* GenerateMaterial(FString Name)
{
	UPackage* TestMaterialPackage = NewObject<UPackage>(nullptr, *(FString(TEXT("/Test/Clothing/TestClothingItem/TestClothingItem")) / Name));
	UMaterial* Material(NewObject<UMaterial>(TestMaterialPackage, FName(Name), RF_Public | RF_Standalone));
	return Material;
}
}

using namespace UE::MetaHuman;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestClothingMaterialsMissing, "MetaHumanSDK.VerifyMetaHumanSkeletalClothing.Materials.Missing",
								EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTestClothingMaterialsMissing::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMetaHumanAssetReport> Report(Test::GenerateReport());
	TStrongObjectPtr<USkeletalMesh> RootItem(Test::GenerateRootObject());
	TStrongObjectPtr<USkeletalMesh> SkelMeshPart(Test::GenerateSkelMesh(TEXT("SK_TestSkelMesh")));
	TStrongObjectPtr<UStaticMesh> StaticMeshPart(Test::GenerateStaticMesh(TEXT("SM_TestStaticMesh")));
	TStrongObjectPtr<USkeletalMesh> ResizingSourcePart(Test::GenerateSkelMesh(TEXT("BodyA_CombinedSkelMesh")));

	// Mesh with no materials at all - generates warning
	SkelMeshPart->SetMaterials({});
	StaticMeshPart->SetStaticMaterials({});
	ResizingSourcePart->SetMaterials({});
	UVerifyMetaHumanSkeletalClothing::VerifyClothingCompatibleAssets(RootItem.Get(), Report.Get());

	if (Algo::AllOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() != TEXT("SK_TestSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Expected to find missing materials warning for Skeletal Mesh, but none was emitted");
	}

	if (Algo::AllOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() != TEXT("SM_TestStaticMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Expected to find missing materials warning for Static Mesh, but none was emitted");
	}

	if (Algo::AnyOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() == TEXT("BodyA_CombinedSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Unexpected missing materials warning emitted for resizing source mesh");
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestClothingMaterialsUnset, "MetaHumanSDK.VerifyMetaHumanSkeletalClothing.Materials.Unset",
								EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTestClothingMaterialsUnset::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMetaHumanAssetReport> Report(Test::GenerateReport());
	TStrongObjectPtr<USkeletalMesh> RootItem(Test::GenerateRootObject());
	TStrongObjectPtr<USkeletalMesh> SkelMeshPart(Test::GenerateSkelMesh(TEXT("SK_TestSkelMesh")));
	TStrongObjectPtr<UStaticMesh> StaticMeshPart(Test::GenerateStaticMesh(TEXT("SM_TestStaticMesh")));
	TStrongObjectPtr<USkeletalMesh> ResizingSourcePart(Test::GenerateSkelMesh(TEXT("BodyA_CombinedSkelMesh")));

	// Mesh with material slots, but no set materials - generates warning
	TArray<FSkeletalMaterial> SkelMeshMaterials;
	SkelMeshMaterials.Add({nullptr, FName(TEXT("EmptyMaterialSlot"))});
	SkelMeshPart->SetMaterials(SkelMeshMaterials);
	TArray<FStaticMaterial> StaticMaterials;
	StaticMaterials.Add({nullptr, FName(TEXT("EmptyMaterialSlot"))});
	StaticMeshPart->SetStaticMaterials(StaticMaterials);
	ResizingSourcePart->SetMaterials(SkelMeshMaterials);
	UVerifyMetaHumanSkeletalClothing::VerifyClothingCompatibleAssets(RootItem.Get(), Report.Get());

	if (Algo::AllOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() != TEXT("SK_TestSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Expected to find missing materials warning for Skeletal Mesh, but none was emitted");
	}

	if (Algo::AllOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() != TEXT("SM_TestStaticMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Expected to find missing materials warning for Static Mesh, but none was emitted");
	}

	if (Algo::AnyOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() == TEXT("BodyA_CombinedSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Unexpected missing materials warning emitted for resizing source mesh");
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestClothingMaterialsPresent, "MetaHumanSDK.VerifyMetaHumanSkeletalClothing.Materials.Present",
								EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTestClothingMaterialsPresent::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UMetaHumanAssetReport> Report(Test::GenerateReport());
	TStrongObjectPtr<USkeletalMesh> RootItem(Test::GenerateRootObject());
	TStrongObjectPtr<USkeletalMesh> SkelMeshPart(Test::GenerateSkelMesh(TEXT("SK_TestSkelMesh")));
	TStrongObjectPtr<UStaticMesh> StaticMeshPart(Test::GenerateStaticMesh(TEXT("SM_TestStaticMesh")));
	TStrongObjectPtr<USkeletalMesh> ResizingSourcePart(Test::GenerateSkelMesh(TEXT("BodyA_CombinedSkelMesh")));

	// Mesh with material slots, but no set materials - generates warning
	TArray<FSkeletalMaterial> SkelMeshMaterials;
	SkelMeshMaterials.Add({Test::GenerateMaterial(TEXT("M_SkelMeshMaterial")), FName(TEXT("TestMaterialSlot"))});
	SkelMeshPart->SetMaterials(SkelMeshMaterials);
	TArray<FStaticMaterial> StaticMaterials;
	StaticMaterials.Add({Test::GenerateMaterial(TEXT("M_StaticMeshMaterial")), FName(TEXT("TestMaterialSlot"))});
	StaticMeshPart->SetStaticMaterials(StaticMaterials);
	ResizingSourcePart->SetMaterials(SkelMeshMaterials);
	UVerifyMetaHumanSkeletalClothing::VerifyClothingCompatibleAssets(RootItem.Get(), Report.Get());

	if (Algo::AnyOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() == TEXT("SK_TestSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Unexpected missing materials warning emitted for  Skeletal Mesh");
	}

	if (Algo::AnyOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() == TEXT("SM_TestStaticMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Unexpected missing materials warning emitted for Static Mesh");
	}

	if (Algo::AnyOf(Report->Warnings, [](const FMetaHumanAssetReportItem& Item)
	{
		return Item.Message.ToString() == TEXT("BodyA_CombinedSkelMesh has not got any Materials assigned");
	}))
	{
		UE_LOGFMT(LogMetaHumanSDK, Error, "Unexpected missing materials warning emitted for resizing source mesh");
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
