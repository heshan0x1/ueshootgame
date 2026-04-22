// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LibWebM : ModuleRules
{
    protected readonly string Version = "1.0.0.27";
    protected readonly string BuildConfig = "Release";
    protected string VersionPath { get => Path.Combine(ModuleDirectory, Version); }
    protected string LibraryPath { get => Path.Combine(VersionPath, "lib"); }
    protected string LibraryName { get => Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ? "libwebm.lib" : "libwebm.a"; }

    public LibWebM(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicSystemIncludePaths.Add(VersionPath);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", Target.Architecture.WindowsLibDir, BuildConfig, LibraryName));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Linux", Target.Architecture.LinuxName, BuildConfig, LibraryName));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Mac", BuildConfig, LibraryName));
        }
    }
}
