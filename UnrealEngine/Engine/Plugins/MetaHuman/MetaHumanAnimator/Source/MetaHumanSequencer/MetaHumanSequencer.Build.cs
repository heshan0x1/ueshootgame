// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanSequencer : ModuleRules
{
	public MetaHumanSequencer(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Engine",
			"MovieScene",
			"MediaCompositing",
			"MovieSceneTools",
			"MovieSceneTracks",
			"Sequencer",
			"CaptureDataCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"UnrealEd",
			"ModelingOperators",
			"GeometryCore",
			"Slate",
			"SlateCore",
			"MediaCompositingEditor",
			"MediaAssets",
			"ImgMedia",
			"ToolMenus",
			"Persona",
			"ControlRig",
			"MetaHumanCaptureData",
			"MetaHumanCore",
		});
	}
}
