// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CompositeEditor : ModuleRules
{
	public CompositeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Composite"
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"CompositeCore",
				"ConcertSyncClient",
				"CoreUObject",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"LevelEditor",
				"MediaAssets",
				"MediaFrameworkUtilities",
				"Projects",
				"PropertyEditor",
				"UnrealEd",
				"SceneOutliner",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "ToolWidgets",
                "WorkspaceMenuStructure",
			}
		);
	}
}
