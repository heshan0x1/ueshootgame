// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionEditor : ModuleRules
	{
        public GeometryCollectionEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetDefinition",
					"Core",
					"CoreUObject",
				    "Slate",
				    "SlateCore",
				    "Engine",
					"EditorFramework",
					"UnrealEd",
				    "PropertyEditor",
				    "RenderCore",
				    "RHI",
                    "GeometryCollectionEngine",
                    "RawMesh",
				    "AssetTools",
				    "AssetRegistry",
				    "SceneOutliner",
					"EditorStyle",
					"ToolMenus",
					"Chaos",
					"MeshDescription",
					"StaticMeshDescription",
					"LevelEditor",
					"InputCore",
					"GraphEditor",
					"DataflowCore",
					"DataflowEngine",
					"DataflowEditor",
				}
			);
			
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ContentBrowser",
					"EditorScriptingUtilities",
				}
			);

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
