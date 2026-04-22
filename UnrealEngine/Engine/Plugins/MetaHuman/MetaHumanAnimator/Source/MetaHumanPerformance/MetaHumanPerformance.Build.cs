// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanPerformance : ModuleRules
{
	public MetaHumanPerformance(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Engine",
			"MetaHumanCaptureData",
			"MetaHumanPipelineCore",
			"MetaHumanPipeline",
			"MetaHumanCoreTech",
			"MetaHumanCoreTechLib"
		});

		if (Target.bBuildEditor == true)
		{
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"MetaHumanCoreEditor",
				"CaptureDataEditor"
			});
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
			});
		}

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Slate",
			"SlateCore",
			"InputCore",
			"EditorFramework",
			"MovieSceneTools",
			"MediaCompositing",
			"MediaCompositingEditor",
			"CinematicCamera",
			"MediaAssets",
			"AnimationCore",
			"Sequencer",
			"LevelSequence",
			"LevelSequenceEditor",
			"MovieScene",
			"MovieSceneTracks",
			"ImgMedia",
			"ToolMenus",
			"ControlRig",
			"ControlRigDeveloper",
			"ControlRigEditor",
			"RigLogicModule",
			"RigVM",
			"RigVMDeveloper",
			"Projects",
			"PropertyEditor",
			"NNE",
			"InteractiveToolsFramework",
			"EditorInteractiveToolsFramework",
			"AdvancedPreviewScene",
			"AssetDefinition",
			"CameraCalibrationCore",
			"LensComponent",
			"ContentBrowserData",
			"MetaHumanCore",
			"MetaHumanCoreEditor",
			"MetaHumanImageViewer",
			"MetaHumanImageViewerEditor",
			"MetaHumanIdentity",
			"MetaHumanIdentityEditor",
			"MetaHumanSequencer",
			"MetaHumanToolkit",
			"MetaHumanFaceContourTracker",
			"MetaHumanFaceAnimationSolver",
			"MetaHumanCaptureDataEditor",
			"MetaHumanPlatform",
			"CaptureDataCore",
			"MetaHumanCaptureUtils",
			"CaptureDataUtils",
			"AudioPlatformConfiguration",
			"MetaHumanSpeech2Face",
		});
	}
}