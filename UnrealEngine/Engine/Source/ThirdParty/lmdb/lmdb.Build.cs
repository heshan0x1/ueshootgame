// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class lmdb : ModuleRules
{
	public lmdb(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string LmdbPath = Path.Combine(Target.UEThirdPartySourceDirectory, "lmdb");
		PublicSystemIncludePaths.Add(LmdbPath);

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LmdbPath, "android", "arm64-v8a", "liblmdb.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LmdbPath, "android", "x86_64", "liblmdb.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LmdbPath, "win64", "lmdb.lib"));
		}
	}
}
