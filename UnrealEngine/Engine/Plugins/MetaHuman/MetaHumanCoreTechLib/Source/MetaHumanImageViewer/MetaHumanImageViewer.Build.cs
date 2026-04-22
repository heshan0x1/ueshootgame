// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanImageViewer : ModuleRules
{
	public MetaHumanImageViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"SlateCore",
			"Slate",
			"InputCore",
			"MetaHumanCoreTech"
		});
	}
}