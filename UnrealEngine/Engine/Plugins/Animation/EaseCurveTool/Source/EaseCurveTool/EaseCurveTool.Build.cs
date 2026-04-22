// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EaseCurveTool : ModuleRules
{
	public EaseCurveTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"CoreUObject",
			"CurveEditor",
			"DeveloperSettings",
			"EditorSubsystem",
			"Engine",
			"InputCore",
			"Json",
			"MovieScene",
			"MovieSceneTools",
			"Projects",
			"PropertyEditor",
			"Sequencer",
			"SequencerCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure"
		});
	}
}
