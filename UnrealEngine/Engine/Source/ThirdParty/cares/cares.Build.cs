// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;
using System.IO;

public class cares : ModuleRules
{
	protected readonly string Version = "1.19.1";
	
	public cares(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		
		string VersionPath = Path.Combine(ModuleDirectory, Version);

		PublicSystemIncludePaths.Add(Path.Combine(VersionPath, "include"));
		
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(VersionPath, "lib", "Unix", Target.Architecture.LinuxName, "Release", "libcares.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(VersionPath, "lib", "Mac", "Release", "libcares.a"));
			PublicSystemLibraries.Add("resolv");
		}
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string ConfigName = "Release";
			PublicAdditionalLibraries.Add(Path.Combine(VersionPath, "lib", "Win64", Target.Architecture.WindowsLibDir, ConfigName, "cares.lib"));
		}

		PublicDefinitions.Add("WITH_CARES");
	}
}
