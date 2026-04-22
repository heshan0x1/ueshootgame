// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GlobalConfigurationDataTests : TestModuleRules
{
	static GlobalConfigurationDataTests()
	{
		TestMetadata = new Metadata()
		{
			TestName = "GlobalConfigurationData",
			TestShortName = "GlobalConfigurationData"
		};
	}

	public GlobalConfigurationDataTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"GlobalConfigurationDataCore"
			});
	}
}