// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProceduralVegetationEditor : ModuleRules
{
	public ProceduralVegetationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedPreviewScene",
				"AssetDefinition",
				"AssetTools",
				"Chaos",
				"ContentBrowser",
				"ContentBrowserData",
				"Core",
				"CoreUObject",
				"CurveEditor",
				"DataflowEditor",
				"Engine",
				"GeometryCollectionEngine",
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				"InputCore",
				"Json",
				"JsonUtilities",
				"LevelEditor",
				"MeshConversionEngineTypes",
				"MeshDescription",
				"MeshConversion",
				"ModelingComponents",
				"PackagesDialog",
				"PCG",
				"PCGEditor",
				"ProceduralVegetation",
				"Projects",
				"PropertyEditor",
				"PlanarCut",
				"RenderCore",
				"Slate",
				"SlateCore",
				"SkeletalMeshDescription",
				"StaticMeshDescription",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"DynamicWind",
				"DynamicWindEditor",
				"RHI"
			}
		);
	}
}