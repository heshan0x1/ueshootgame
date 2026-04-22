// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class Embree : ModuleRules
{
	public Embree(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDependencyModuleNames.Add("IntelTBB");

		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);

		string DeploymentDirectory = Path.Combine(ModuleDirectory, "Deploy", "embree-4.3.3");

		PublicSystemIncludePaths.Add(Path.Combine(DeploymentDirectory, "include"));

		string LibPostfix = bDebug ? "_d" : "";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string PlatformDirectory = Path.Combine(
				DeploymentDirectory,
				"VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(),
				Target.Architecture.WindowsLibDir);

			string ArchDirectory = (Target.Architecture == UnrealArch.Arm64) ? "arm64" : "";
			string BinDirectory = Path.Combine(PlatformDirectory, "bin");
			string LibDirectory = Path.Combine(PlatformDirectory, "lib");

			PublicSystemLibraryPaths.Add(LibDirectory);

			string EmbreeDllName = "embree4" + LibPostfix + ".dll";
			string EmbreeLibName = "embree4" + LibPostfix + ".lib";

			PublicAdditionalLibraries.Add(
				Path.Combine(LibDirectory, EmbreeLibName));

			RuntimeDependencies.Add(
				Path.Combine("$(TargetOutputDir)", ArchDirectory, EmbreeDllName),
				Path.Combine(BinDirectory, EmbreeDllName));

			PublicDelayLoadDLLs.Add(EmbreeDllName);

			PublicDefinitions.Add("USE_EMBREE=1");
			PublicDefinitions.Add("USE_EMBREE_MAJOR_VERSION=4");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Mac",
				"lib");

			List<string> EmbreeLibraries = new List<string> {
				"embree4" + LibPostfix,
				"embree4" + LibPostfix + ".4",
				"embree4" + LibPostfix + ".4.3.3"
			};

			foreach (string EmbreeLibrary in EmbreeLibraries)
			{
				string EmbreeLibName = "lib" + EmbreeLibrary + ".dylib";

				PublicAdditionalLibraries.Add(
					Path.Combine(LibDirectory, EmbreeLibName));

				RuntimeDependencies.Add(
					Path.Combine("$(TargetOutputDir)", EmbreeLibName),
					Path.Combine(LibDirectory, EmbreeLibName));
			}

			PublicDefinitions.Add("USE_EMBREE=1");
			PublicDefinitions.Add("USE_EMBREE_MAJOR_VERSION=4");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string LibDirectory = Path.Combine(
				DeploymentDirectory,
				"Unix",
				Target.Architecture.LinuxName,
				"lib");

			List<string> EmbreeLibraries = new List<string> {
				"embree4" + LibPostfix + ".so",
				"embree4" + LibPostfix + ".so.4",
				"embree4" + LibPostfix + ".so.4.3.3"
			};

			foreach (string EmbreeLibrary in EmbreeLibraries)
			{
				string EmbreeLibName = "lib" + EmbreeLibrary;

				// The shared library file names with a version suffix
				// confuse the extension stripping that UBT does, causing it
				// to pass extra malformed '-l' options to the linker. We only
				// need to include one anyway, so only add the non-suffixed
				// ".so" as an additional library.
				if (EmbreeLibName.EndsWith(".so"))
				{
					PublicAdditionalLibraries.Add(
						Path.Combine(LibDirectory, EmbreeLibName));
				}

				RuntimeDependencies.Add(
					Path.Combine("$(TargetOutputDir)", EmbreeLibName),
					Path.Combine(LibDirectory, EmbreeLibName));
			}

			PublicDefinitions.Add("USE_EMBREE=1");
			PublicDefinitions.Add("USE_EMBREE_MAJOR_VERSION=4");
		}
		else
		{
			PublicDefinitions.Add("USE_EMBREE=0");
		}
	}
}
