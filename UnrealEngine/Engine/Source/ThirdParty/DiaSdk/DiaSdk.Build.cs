// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class DiaSdk : ModuleRules
{
	public DiaSdk(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
		{
			string DiaSdkDir = Target.WindowsPlatform.DiaSdkDir ?? throw new System.Exception("Unable to find DIA SDK directory");
			string Arch = (Target.Architecture == UnrealArch.Arm64) ? "arm64" : "amd64";
			string RuntimeArchDir = (Target.Architecture == UnrealArch.Arm64) ? "arm64" : String.Empty;
			PublicDefinitions.Add("WITH_DIASDK=1");
			PublicSystemIncludePaths.Add(Path.Combine(DiaSdkDir, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(DiaSdkDir, "lib", Arch, "diaguids.lib"));
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", RuntimeArchDir, "msdia140.dll"), Path.Combine(DiaSdkDir, "bin", Arch, "msdia140.dll"));

#if UE_5_5_OR_LATER // retain compatibility with separate test/dev projects
			ExtraRootPath = ("DiaSdk", DiaSdkDir);
#endif
		}
		else
		{
			PublicDefinitions.Add("WITH_DIASDK=0");
		}
	}
}