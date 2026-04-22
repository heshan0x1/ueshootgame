// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class metis : ModuleRules
{
    protected readonly string Version = "5.2.1";
    protected string VersionPath { get => Path.Combine(ModuleDirectory, Version); }
    protected string LibraryPath { get => Path.Combine(VersionPath, "lib"); }
    protected string BuildConfig { get => Target.Configuration == UnrealTargetConfiguration.Debug ? "Debug" : "Release"; }

    public metis(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicSystemIncludePaths.Add(Path.Combine(VersionPath, "include"));

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "GKlib"
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", Target.Architecture.WindowsLibDir, BuildConfig, "metis.lib"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Linux", Target.Architecture.LinuxName, BuildConfig, "libmetis.a"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", BuildConfig, "libmetis.a"));
        }
    }
}
