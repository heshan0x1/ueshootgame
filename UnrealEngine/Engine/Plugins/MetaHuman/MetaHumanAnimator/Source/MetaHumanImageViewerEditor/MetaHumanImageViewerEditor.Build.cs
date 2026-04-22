// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanImageViewerEditor : ModuleRules
{
	public MetaHumanImageViewerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"EditorStyle",
			"SlateCore",
			"MediaAssets",
			"ProceduralMeshComponent",
			"MetaHumanImageViewer",
			"MetaHumanCore",
			"MetaHumanCoreTech"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"Engine",
			"Slate",
			"InputCore",
			"EditorStyle",
			"UnrealEd",
			"CaptureDataCore",
			"MetaHumanCoreEditor",
		});
	}
}
