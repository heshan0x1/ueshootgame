// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LearningAgents : ModuleRules
{
	public LearningAgents(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Learning",
			});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"NNE",
				"NNERuntimeBasicCpu",
				"Json",
				"JsonUtilities",
			});
	}
}
