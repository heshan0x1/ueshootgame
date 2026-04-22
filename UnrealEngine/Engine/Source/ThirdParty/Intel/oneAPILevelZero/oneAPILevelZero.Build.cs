// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class oneAPILevelZero : ModuleRules
{
	protected virtual string oneAPILevelZeroVersion
	{
		get
		{
			return "1.21.9";
		}
	}

	protected virtual string oneAPILibName
	{
		get
		{
			return "ze_loader";
		}
	}

	private string ProjectBinariesDir
	{
		get
		{
			return "$(TargetOutputDir)";
		}
	}

	protected virtual string IncRootDirectory { get { return ModuleDirectory; } }
	protected virtual string LibRootDirectory { get { return PlatformModuleDirectory; } }
	protected virtual string oneAPILevelZeroIncPath{ get { return Path.Combine(IncRootDirectory, oneAPILevelZeroVersion, "include"); } }
	protected virtual string oneAPILevelZeroLibPath { get { return Path.Combine(LibRootDirectory, oneAPILevelZeroVersion, "lib"); } }	
	protected virtual string oneAPILevelZeroDllPath { get { return Path.Combine(LibRootDirectory, oneAPILevelZeroVersion, "Binaries"); } }

	public oneAPILevelZero(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicSystemIncludePaths.Add(oneAPILevelZeroIncPath);
		
		string DllRoot = "";

		string[] DllNames = [oneAPILibName, "ze_null", "ze_tracing_layer", "ze_validation_layer"];
		List<string> PlatformDllNames = new List<string>();


		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) && Target.Architecture.bIsX64)
		{
			string LibRoot = Path.Combine(oneAPILevelZeroLibPath, "Win64");
			LibRoot = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT
				? Path.Combine(LibRoot, "Debug")
				: Path.Combine(LibRoot, "Release");


			foreach(string DllName in DllNames)
			{
				string PlatformLibName =  $"{DllName}.lib";
				string PlatformLibPath = Path.Combine(LibRoot, PlatformLibName);
				if (File.Exists(PlatformLibPath))
				{
					PublicAdditionalLibraries.Add(PlatformLibPath);
				}
			}

			
			DllRoot = Path.Combine(oneAPILevelZeroDllPath, "Win64");
			DllRoot = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT
				? Path.Combine(DllRoot, "Debug")
				: Path.Combine(DllRoot, "Release");
				
			
			foreach(string DllName in DllNames)
			{
				string PlatformDllName =  $"{DllName}.dll";
				string BaseDllPath = Path.Combine(DllRoot, PlatformDllName);
				if (File.Exists(BaseDllPath))
				{
					PlatformDllNames.Add(PlatformDllName);
				}
			}

		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Type == TargetType.Server)
			{
				string Err = string.Format("{0} dedicated server is made to depend on {1}. We want to avoid this, please correct module dependencies.", Target.Platform.ToString(), this.ToString());
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			DllRoot = Path.Combine(oneAPILevelZeroDllPath, "Unix", Target.Architecture.LinuxName);
			DllRoot = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT
				? Path.Combine(DllRoot, "Debug")
				: Path.Combine(DllRoot, "Release");

			foreach(string DllName in DllNames)
			{
				string PlatformDllName =  $"lib{DllName}.so";
				string BaseDllPath = Path.Combine(DllRoot, PlatformDllName);
				if (File.Exists(BaseDllPath))
				{
					PlatformDllNames.Add(PlatformDllName);
					PublicAdditionalLibraries.Add(BaseDllPath);
				}
			}
		}

		if (PlatformDllNames.Count > 0)
		{
			foreach(string PlatformDllName in PlatformDllNames)
			{
				string BaseDllPath = Path.Combine(DllRoot, PlatformDllName);
				string TargetDllPath = Path.Combine(ProjectBinariesDir, PlatformDllName);
				RuntimeDependencies.Add(
					TargetDllPath,
					BaseDllPath,
					StagedFileType.NonUFS);
			}
			PublicRuntimeLibraryPaths.Add(ProjectBinariesDir);
		}
	}
}
