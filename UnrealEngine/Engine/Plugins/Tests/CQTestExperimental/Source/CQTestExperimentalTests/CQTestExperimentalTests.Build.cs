// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CQTestExperimentalTests : ModuleRules
{
	public CQTestExperimentalTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"NetCore",
					"CQTest"
				 }
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EngineSettings",
					"LevelEditor",
					"UnrealEd"
			});
		}

		SetupIrisSupport(Target);
	}
}
