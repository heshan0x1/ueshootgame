// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TrackingAlignment : ModuleRules
{
	public TrackingAlignment(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"OpenCVHelper",
				"OpenCV",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
