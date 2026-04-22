// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;

public class UEOpenAPV : ModuleRules
{
	public UEOpenAPV(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string IncPath = Path.Combine(ModuleDirectory, "Deploy/include");
			PublicSystemIncludePaths.Add(IncPath);

			string ArchPath = "Win64"; //(Target.Architecture == UnrealArch.Arm64) ? "WinArm64" : "Win64";
			string LibPath = Path.Combine(ModuleDirectory, "Deploy", ArchPath, "lib");
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "oapv", "oapv.lib"));
		}
	}
}