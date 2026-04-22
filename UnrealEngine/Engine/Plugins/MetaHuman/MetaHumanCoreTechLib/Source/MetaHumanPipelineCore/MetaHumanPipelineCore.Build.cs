// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanPipelineCore : ModuleRules
{
	public MetaHumanPipelineCore(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bUseOpenCV = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Eigen",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"EventLoop",
			"ModelingOperators",
			"NNE",
			"AudioPlatformConfiguration",
			"CaptureDataCore",
			"MetaHumanCoreTech",
			"MetaHumanCaptureData",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		if (bUseOpenCV)
		{
			PrivateDefinitions.Add("USE_OPENCV");
			PrivateDependencyModuleNames.Add("OpenCVHelper");
			PrivateDependencyModuleNames.Add("OpenCV");
		}
	}
}
