// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosClothAssetTools : ModuleRules
{
	public ChaosClothAssetTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
			"MeshConversion",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Engine",
				"CoreUObject",
				"RenderCore",
				"RHI",
				"Chaos",
				"ChaosCloth",
				"ChaosClothEditor",  // For Chaos::FSimulationEditorExtender
				"ChaosClothAsset",
				"ChaosClothAssetEngine",
				"DataflowEngine",
				"SlateCore",
				"UnrealEd",
				"Projects",
				"ClothPainter",                 // For clothing asset exporter
				"ClothingSystemRuntimeCommon",
				"ClothingSystemEditorInterface",  // For UChaosClothAssetSKMClothingAssetFactory
				"GeometryCore",					// For DynamicMesh conversion
				"MeshConversion",
				"MeshDescription",
				"SkeletalMeshDescription",		// For FSkeletalMeshAttributes::DefaultSkinWeightProfileName
				"SkeletalMeshUtilitiesCommon"
			}
		);
	}
}
