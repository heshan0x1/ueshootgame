// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SlateIMInGame : ModuleRules
{
	public SlateIMInGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateIM",
			}
		);
	}
}