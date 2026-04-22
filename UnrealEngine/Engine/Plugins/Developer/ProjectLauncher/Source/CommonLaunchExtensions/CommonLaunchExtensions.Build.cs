// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CommonLaunchExtensions : ModuleRules
{
	public CommonLaunchExtensions(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
		[
			"Core",
			"CoreUObject",
			"Json",
			"Slate",
			"SlateCore",
			"Sockets",
			"TraceLog",
			"ProjectLauncher",
			"DeveloperSettings"
		]);


		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
			[
				"UnrealEd",
				"Engine"
			]);
		}
	}
}
