// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IREEUtils : ModuleRules
{
	public IREEUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core"
			}
		);
	}
}
