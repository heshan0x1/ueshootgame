// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OnlineFrameworkCommon : ModuleRules
{
	public OnlineFrameworkCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreOnline",
				"CoreUObject",
				"Engine",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"OnlineServicesCommonEngineUtils",
				"OnlineServicesInterface",
				"OnlineServicesNull",
				"OnlineServicesOSSAdapter",
				"OnlineSubsystem",
			});
	}
}
