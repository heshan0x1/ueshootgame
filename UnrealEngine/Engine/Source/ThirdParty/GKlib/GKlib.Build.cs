// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GKlib : ModuleRules
{
    protected readonly string Version = "8bd6bad";
    protected string VersionPath { get => Path.Combine(ModuleDirectory, Version); }
    protected string LibraryPath { get => Path.Combine(VersionPath, "lib"); }
    protected string BuildConfig { get => Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release"; }

    public GKlib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicSystemIncludePaths.Add(VersionPath);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", Target.Architecture.WindowsLibDir, BuildConfig, "GKlib.lib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", BuildConfig, "libGKlib.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Linux", Target.Architecture.LinuxName, BuildConfig, "libGKlib.a"));
        }
    }
}
