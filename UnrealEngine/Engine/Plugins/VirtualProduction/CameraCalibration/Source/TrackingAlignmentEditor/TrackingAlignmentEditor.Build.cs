// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TrackingAlignmentEditor : ModuleRules
{
	public TrackingAlignmentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Blutility",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"TrackingAlignment",
				"UMGEditor"
			}
		);
	}
}
