// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CinematicSequenceNavigator : ModuleRules
{
	public CinematicSequenceNavigator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CineAssemblyTools",
			"CoreUObject",
			"Engine",
			"MovieScene",
			"SequenceNavigator",
			"Sequencer",
			"SequencerCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
