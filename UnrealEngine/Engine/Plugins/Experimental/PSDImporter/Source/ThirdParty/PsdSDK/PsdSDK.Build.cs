// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class PsdSDK : ModuleRules
{
	public PsdSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		bool bIsDebugConfig = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string ConfigName = bIsDebugConfig ? "Debug" : "Release";
		
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "Includes"));

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Libraries", Target.Platform.ToString(), Target.Architecture.WindowsLibDir, ConfigName, "Psd.lib"));
			PublicDefinitions.Add("WITH_PSD");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// TODO : Add supported libraries for Mac
			//PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Libraries", Target.Platform.ToString(), ConfigName, "Psd.a"));

			PublicDefinitions.Add("WITH_PSD");

		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			// TODO : Add supported libraries for Linux
			//PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Libraries", Target.Platform.ToString(), ConfigName, "Psd.a"));

			PublicDefinitions.Add("WITH_PSD");
		}
	}
}
