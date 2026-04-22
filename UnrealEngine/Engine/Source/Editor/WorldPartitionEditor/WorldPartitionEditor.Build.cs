// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WorldPartitionEditor : ModuleRules
{
    public WorldPartitionEditor(ReadOnlyTargetRules Target) : base(Target)
    {     
        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"ApplicationCore",
				"ContentBrowser",
				"ContentBrowserData",
				"Core",
				"CoreUObject",
				"DataLayerEditor",
				"DeveloperSettings",
				"EditorFramework",
				"EditorSubsystem",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"LevelEditor",	
				"MainFrame",
				"PropertyEditor",				
				"SceneOutliner",
				"Slate",
				"SlateCore",
				"RenderCore",
				"Renderer",
				"RHI",				
				"ToolMenus",
				"UnrealEd",
				"WorldBrowser"
			}
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
				"AssetTools",
            }
		);

		PrivateIncludePathModuleNames.AddRange
		(
			new string[]
			{
				"WorkspaceMenuStructure",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
	}
}
