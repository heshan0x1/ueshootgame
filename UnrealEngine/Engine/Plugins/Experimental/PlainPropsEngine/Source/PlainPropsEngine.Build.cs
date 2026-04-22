// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PlainPropsEngine : ModuleRules
{
	public PlainPropsEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		bDisableStaticAnalysis = true;

		CppStandard = CppStandardVersion.Cpp20;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"CorePreciseFP",
				"Engine",
				"PlainProps",
				"PlainPropsUObject"
		});
	}
}
