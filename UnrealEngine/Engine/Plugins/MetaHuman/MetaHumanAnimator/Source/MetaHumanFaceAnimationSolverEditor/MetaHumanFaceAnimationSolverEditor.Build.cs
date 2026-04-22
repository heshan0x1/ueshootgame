// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFaceAnimationSolverEditor : ModuleRules
{
	public MetaHumanFaceAnimationSolverEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"UnrealEd",
			"AssetDefinition",
			"SlateCore",
			"Slate",
			"PropertyEditor",
			"MetaHumanFaceAnimationSolver",
			"MetaHumanCoreEditor",
			"MetaHumanConfig",
			"MetaHumanConfigEditor",
		});
	}
}
