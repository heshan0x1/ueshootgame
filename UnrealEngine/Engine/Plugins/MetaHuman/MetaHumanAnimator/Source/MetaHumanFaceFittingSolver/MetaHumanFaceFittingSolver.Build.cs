// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFaceFittingSolver : ModuleRules
{
	public MetaHumanFaceFittingSolver(ReadOnlyTargetRules Target) : base(Target)
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
			"MetaHumanFaceAnimationSolver",
			"MetaHumanConfig",
		});
	}
}
