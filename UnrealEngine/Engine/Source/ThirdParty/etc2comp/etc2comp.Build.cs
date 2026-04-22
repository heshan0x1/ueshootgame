// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class etc2comp : ModuleRules
{
	public etc2comp(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_ETC2COMP=1");
		
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "EtcLib", "Etc"));
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "EtcLib", "EtcCodec"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			bool bUseDebugLibs = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT;
			string ConfigName = bUseDebugLibs ? "Debug" : "Release";
			string ArchName = Target.Architecture == UnrealArch.Arm64 ? "WinArm64" : "Win64";

			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", ArchName, ConfigName, "EtcLib.lib"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Mac", "Release", "libEtcLib.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Linux", "Release", "libEtcLib.a"));
		}
	}
}

