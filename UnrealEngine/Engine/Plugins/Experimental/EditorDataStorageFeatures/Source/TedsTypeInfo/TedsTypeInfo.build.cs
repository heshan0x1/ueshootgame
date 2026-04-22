// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Experimental test module. please refrain from depending on it until this warning is removed
public class TedsTypeInfo : ModuleRules
{
	public TedsTypeInfo(ReadOnlyTargetRules Target) : base(Target)
	{
		// Enable truncation warnings in this module
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

		ShortName = "TEDSTypeInfo";

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] { });
			PrivateIncludePaths.AddRange(new string[] { });

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ClassViewer",
					"Core",
					"CoreUObject",
					"Engine",
					"TypedElementFramework",
					"UnrealEd",
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
				});

		}
	}
}
