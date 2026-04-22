// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DataflowEditor : ModuleRules
	{
		public DataflowEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
					Path.Combine(GetModuleDirectory("GeometryCacheEd"), "Private"),
					Path.Combine(GetModuleDirectory("SkeletalMeshModelingTools"), "Private")
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"TedsOutliner",
					"SkeletalMeshModelingTools"
				}
			);

			PublicIncludePathModuleNames.AddRange(
				new string[]
				{
					"SkeletonEditor"
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AdvancedPreviewScene",
					"ApplicationCore",
					"AssetDefinition",
					"AssetTools",
					"AssetRegistry",
					"BaseCharacterFXEditor",
					"BlueprintGraph",
					"Chaos",
					"Core",
					"CoreUObject",
					"DataflowAssetTools",
					"DataflowCore",
					"DataflowEngine",
					"DataflowEnginePlugin",
					"DataflowNodes",
					"DataflowSimulation",
					"DetailCustomizations",
					"DeveloperSettings",
					"DynamicMesh",
					"Engine",
					"EditorFramework",
					"EditorInteractiveToolsFramework",
					"EditorStyle",
					"GeometryCache",
					"GeometryCore",
					"GeometryCollectionEngine",
					"GeometryFramework",
					"GraphEditor",
					"InputCore",
					"InteractiveToolsFramework",
					"LevelEditor",
					"MeshDescription",
					"MeshConversion",
					"MeshModelingTools",
					"MeshModelingToolsEditorOnly",
					"MeshModelingToolsEditorOnlyExp",
					"MeshModelingToolsExp",
					"ModelingComponentsEditorOnly",
					"ModelingComponents",
					"ModelingToolsEditorMode",
					"PlanarCut",
					"Projects",
					"PropertyEditor",
					"RenderCore",
					"RHI",
					"SceneOutliner",
					"TedsOutliner",
					"SharedSettingsWidgets",
					"SkeletonEditor",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"ToolMenus",
					"ToolWidgets",
					"TypedElementRuntime",
					"TypedElementFramework",
					"UnrealEd",
					"WorkspaceMenuStructure",
					"XmlParser",
					"EditorWidgets",
					"KismetWidgets",      // SScrubControlPanel
					"AnimGraph", 
					"ChaosCaching", // UAnimSingleNodeInstance
					"StructUtilsEditor",
					"MessageLog",
					"AppFramework",
					"SkeletalMeshDescription",
					"SequencerWidgets",
					"TimeManagement",
					"GeometryCacheEd",
					"WidgetRegistration",
				}
			);
		}
	}
}
