// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LearningAgentsTraining : ModuleRules
{
	public LearningAgentsTraining(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AIModule",
				"Core",
				"GameplayTags",
				"NavigationSystem",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"Learning",
				"LearningAgents",
				"LearningTraining",
			});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
