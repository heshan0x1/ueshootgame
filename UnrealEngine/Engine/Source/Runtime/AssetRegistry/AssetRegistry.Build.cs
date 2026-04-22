// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AssetRegistry : ModuleRules
{
	public AssetRegistry(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"TraceLog",
				"ApplicationCore",
				"Projects",
				"TelemetryUtils",
				"PakFile"
			}
			);

		if (Target.bBuildEditor == true)
		{
			PrivateIncludePathModuleNames.AddRange(new string[] { "DirectoryWatcher", "TargetPlatform" });
			DynamicallyLoadedModuleNames.AddRange(new string[] { "DirectoryWatcher", "TargetPlatform" });
		}

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
