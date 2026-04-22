// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MusicEnvironmentEditor : ModuleRules
{
	public MusicEnvironmentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"MusicEnvironment",
				"PropertyEditor",
				"Slate",
				"SlateCore"
			}
		);
	}
}