// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BootstrapPackagedGame : ModuleRules
{
	public BootstrapPackagedGame(ReadOnlyTargetRules Target) : base(Target)
	{
		bTreatAsEngineModule = true; // For internal headers

		PrivateIncludePathModuleNames.Add("ApplicationCore");

		PublicSystemLibraries.Add("shlwapi.lib");
		PublicSystemLibraries.Add("version.lib");
	}
}
