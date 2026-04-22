// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PCGEditor : ModuleRules
	{
		public PCGEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Projects",
					"Engine",
					"CoreUObject",
					"PlacementMode", 
					"PCG"
				});

			if (Target.WithAutomationTests)
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"LevelEditor"
					});
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AdvancedPreviewScene",
					"AppFramework",
					"ApplicationCore",
					"AssetDefinition",
					"AssetRegistry",
					"AssetTools",
					"BlueprintGraph",
					"ContentBrowser",
					"ContentBrowserData",
					"DesktopWidgets",
					"DetailCustomizations",
					"DeveloperSettings",
					"EditorFramework",
					"EditorScriptingUtilities",
					"EditorStyle",
					"EditorSubsystem",
					"EditorWidgets",
					"GameProjectGeneration",
					"GraphEditor",
					"InputCore",
					"InteractiveToolsFramework",
					"Kismet",
					"KismetWidgets",
					"LevelEditor",
					"PropertyEditor",
					"RHI",
					"RenderCore",
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"SourceControl",
					"StatusBar",
					"StructUtilsEditor",
					"SubobjectEditor",
					"SubobjectDataInterface",
					"ToolMenus",
					"ToolWidgets",
					"TypedElementFramework",
					"TypedElementRuntime",
					"UnrealEd",
					"WidgetRegistration", 
					"ModelingComponents",
					"ModelingComponentsEditorOnly",
					"Landscape",
					"GeometryCore",
					"GeometryFramework"
				});
		}
	}
}
