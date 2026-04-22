// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFEditor : ModuleRules
	{
		public UAFEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"AssetTools",
					"UnrealEd",
					"UAF",
					"UAFUncookedOnly",
					"UnrealEd",
					"SlateCore",
					"AnimationCore",
					"Slate",
					"InputCore",
					"PropertyEditor",
					"RigVM",
					"RigVMEditor",
					"RigVMDeveloper",
					"GraphEditor",
					"ToolWidgets",
					"ToolMenus",
					"AssetDefinition",
					"SourceControl", 
					"KismetWidgets",
					"StructUtilsEditor",
					"BlueprintGraph",	// For K2 Schema
					"DesktopWidgets",
					"ContentBrowserFileDataSource",
					"SubobjectEditor",
					"Settings",
					"EditorWidgets",
					"WorkspaceEditor",
					"ContentBrowser",
					"UniversalObjectLocator",
					"UniversalObjectLocatorEditor",
					"Kismet",
					"AdvancedWidgets", 
					"SceneOutliner",
					"EditorFramework",
					"InteractiveToolsFramework",
					"EditorInteractiveToolsFramework",
					"TraceAnalysis",
					"TraceLog",
					"TraceServices",
					"TraceInsights",
					"RewindDebuggerInterface",
					"GameplayInsights",
					"Projects",
					"Persona",
					"DeveloperSettings",
					"AdvancedPreviewScene"
				}
			);
		}
	}
}