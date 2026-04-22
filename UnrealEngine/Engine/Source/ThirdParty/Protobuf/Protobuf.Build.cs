// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Protobuf : ModuleRules
{
	protected readonly string Version = "30.0";

	public Protobuf(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDependencyModuleNames.Add("zlib");
		PublicDependencyModuleNames.Add("abseil");

		string VersionPath = Path.Combine(ModuleDirectory, Version);
		string LibraryPath = Path.Combine(VersionPath, "lib");

		PublicSystemIncludePaths.Add(Path.Combine(VersionPath, "include"));

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Unix", Target.Architecture.LinuxName, "Release", "libprotobuf.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Unix", Target.Architecture.LinuxName, "Release", "libutf8_range.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Unix", Target.Architecture.LinuxName, "Release", "libutf8_validity.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Unix", Target.Architecture.LinuxName, "Release", "libupb.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", "Release", "libprotobuf.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", "Release", "libutf8_range.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", "Release", "libutf8_validity.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", "Release", "libupb.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string ArchLibDir = Target.Architecture.WindowsLibDir;
			if (Target.WindowsPlatform.Compiler.IsClang())
			{
				ArchLibDir = $"{ArchLibDir}-clang";
			}
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", ArchLibDir, "Release", "libprotobuf.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", ArchLibDir, "Release", "libutf8_range.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", ArchLibDir, "Release", "libutf8_validity.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", ArchLibDir, "Release", "libupb.lib"));
		}

		PublicDefinitions.Add("WITH_PROTOBUF");
	}
}
