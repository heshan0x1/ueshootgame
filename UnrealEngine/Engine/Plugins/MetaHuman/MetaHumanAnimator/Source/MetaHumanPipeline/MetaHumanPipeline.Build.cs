// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanPipeline : ModuleRules
{
	public MetaHumanPipeline(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"Eigen",
			"MetaHumanCore",
			"MetaHumanSpeech2Face",
			"MetaHumanCoreTech",
			"MetaHumanCoreTechLib",
			"MetaHumanPipelineCore",
			"MeshTrackerInterface",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"ModelingOperators",
			"NNE",
			"Engine",
			"Json",
			"AudioPlatformConfiguration",
			"MetaHumanCaptureData",
			"GeometryCore",
			"Projects",
			"RenderCore",
			"MetaHumanConfig",
			"MetaHumanFaceAnimationSolver",
			"MetaHumanPlatform",
			"CaptureDataCore",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
