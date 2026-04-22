// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanToolkit : ModuleRules
{
	public MetaHumanToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"AdvancedPreviewScene",
			"MovieScene",
			"Sequencer",
			"ImgMedia",
			"MovieSceneTracks",
			"MetaHumanCaptureData",
			"MetaHumanImageViewer",
			"MetaHumanImageViewerEditor",
			"MetaHumanSequencer",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"InputCore",
			"ToolMenus",
			"PropertyEditor",
			"Projects",
			"MediaAssets",
			"ProceduralMeshComponent",
			"RenderCore",
			"MetaHumanCore",
			"CaptureDataCore"
		});
	}
}
