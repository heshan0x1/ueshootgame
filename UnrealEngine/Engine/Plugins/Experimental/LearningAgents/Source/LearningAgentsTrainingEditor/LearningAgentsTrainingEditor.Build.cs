// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LearningAgentsTrainingEditor : ModuleRules
{
	public LearningAgentsTrainingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			});

		PrivateIncludePaths.AddRange(
			new string[] {
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Learning",
				"LearningAgents",
				"LearningTraining",
				"LearningAgentsTraining",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"PropertyEditor",
				"UnrealEd",
				"UMG"
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			});
	}
}
