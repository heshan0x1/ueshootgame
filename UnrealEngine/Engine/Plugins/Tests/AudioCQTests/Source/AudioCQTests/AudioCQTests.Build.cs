// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioCQTests : ModuleRules
{
	public AudioCQTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"AudioMixer",
					"Core",
					"CQTest",
					"Engine",
				 }
			);
	}
}
