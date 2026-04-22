// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanControlsConversionTest : ModuleRules
{
	public MetaHumanControlsConversionTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.AddRange(new string[] 
		{
			// ... add public include paths required here ...
		});


		PrivateIncludePaths.AddRange(new string[] 
		{
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MetaHumanCore",
			"MetaHumanCoreTech",
			"MetaHumanConfig",
			"RigLogicModule",
			"Json",
			"Projects",
			"MeshTrackerInterface", 
			"MetaHumanSDKRuntime",
		});
	}
}
