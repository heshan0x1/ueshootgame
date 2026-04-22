// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFaceContourTracker : ModuleRules
{
	public MetaHumanFaceContourTracker(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"NNE",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"Slate",
			"SlateCore",
			"Projects",
			"MetaHumanCore",
		});
	}
}