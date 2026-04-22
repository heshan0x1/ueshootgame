// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFaceAnimationSolver : ModuleRules
{
	public MetaHumanFaceAnimationSolver(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Projects",
			"Json",
			"Slate",
			"SlateCore",
			"MetaHumanConfig",
		});
	}
}
