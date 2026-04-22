// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MusicEnvironmentTests : ModuleRules
{
	public MusicEnvironmentTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"MusicEnvironment",
			}
		);
	}
}