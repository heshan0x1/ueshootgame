// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class NNEMlirTools : ModuleRules
{
	public NNEMlirTools(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Internal"));
		
		string LibPlatformRelativePath = Path.Combine("Binaries", "ThirdParty", "NNEMlirTools");
		string SharedLibFileName = "UNSUPPORTED_PLATFORM";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibPlatformRelativePath = Path.Combine(LibPlatformRelativePath, /*Target.Architecture == UnrealArch.Arm64 ? "WinArm64" : */"Win64");
			string LibPlatformPath = Path.Combine(PluginDirectory, LibPlatformRelativePath);

			SharedLibFileName = "NNEMlirTools.dll";

			PublicDelayLoadDLLs.Add(SharedLibFileName);
			RuntimeDependencies.Add(Path.Combine(LibPlatformPath, SharedLibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux/* || Target.Platform == UnrealTargetPlatform.LinuxArm64*/)
		{
			LibPlatformRelativePath = Path.Combine(LibPlatformRelativePath, Target.Platform.ToString());
			string LibPlatformPath = Path.Combine(PluginDirectory, LibPlatformRelativePath);

			SharedLibFileName = "libNNEMlirTools.so";
			
			PublicDelayLoadDLLs.Add(Path.Combine(LibPlatformPath, SharedLibFileName));
			RuntimeDependencies.Add(Path.Combine(LibPlatformPath, SharedLibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			LibPlatformRelativePath = Path.Combine(LibPlatformRelativePath, Target.Platform.ToString());
			string LibPlatformPath = Path.Combine(PluginDirectory, LibPlatformRelativePath);

			SharedLibFileName = "libNNEMlirTools.dylib";

			PublicDelayLoadDLLs.Add(Path.Combine(LibPlatformPath, SharedLibFileName));
			RuntimeDependencies.Add(Path.Combine(LibPlatformPath, SharedLibFileName));
		}

		if (Target.Type == TargetType.Editor)
		{
			bEnableExceptions = true;
		}
		else
		{
			PublicDefinitions.Add("NNEMLIR_NO_EXCEPTIONS");
		}

		PublicDefinitions.Add("NNEMLIRTOOLS_SHAREDLIB_PATH=" + Path.Combine(LibPlatformRelativePath, SharedLibFileName).Replace('\\', '/'));
	}
}