// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class SDL3 : ModuleRules
{
	protected virtual string SDL3Version { get { return "SDL-gui-backend"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string SDL3IncPath { get { return Path.Combine(IncRootDirectory, "SDL3", SDL3Version, "include"); } }
	protected virtual string SDL3LibPath { get { return Path.Combine(LibRootDirectory, "SDL3", SDL3Version, "lib"); } }

	public SDL3(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// assume SDL to be built with extensions
		PublicDefinitions.Add("SDL_WITH_EPIC_EXTENSIONS=1");

		PublicSystemIncludePaths.Add(SDL3IncPath);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				// Debug version should be built with -fPIC and usable in all targets
				PublicAdditionalLibraries.Add(Path.Combine(SDL3LibPath, "Unix", Target.Architecture.LinuxName, "libSDL3_fPIC_Debug.a"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(SDL3LibPath, "Unix", Target.Architecture.LinuxName, "libSDL3_fPIC.a"));
			}
		}
	}
}
