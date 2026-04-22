// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class FreeImage : ModuleRules
{
	public FreeImage(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "FreeImage-3.18.0", "Dist"));

		string BinaryLibraryFolder = Path.Combine(Target.UEThirdPartyBinariesDirectory, "FreeImage", Target.Platform.ToString());
		string LibraryFileName = "";
		bool bWithFreeImage = false;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibraryFileName = "FreeImage.dll";
			if (Target.Architecture == UnrealArch.Arm64)
			{
				BinaryLibraryFolder = Path.Combine(BinaryLibraryFolder, "arm64");
			}			
			string DynLibPath = Path.Combine(BinaryLibraryFolder, LibraryFileName);

			string LibArchPath = (Target.Architecture == UnrealArch.Arm64) ? "WinArm64" : "Win64";
			string LibPath = Path.Combine(ModuleDirectory, "lib", LibArchPath);
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "FreeImage.lib"));

			PublicDelayLoadDLLs.Add(LibraryFileName);
			RuntimeDependencies.Add(DynLibPath);
			bWithFreeImage = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			LibraryFileName = "libfreeimage-3.18.0.so";
			string DynLibPath = Path.Combine(BinaryLibraryFolder, LibraryFileName);

			PublicRuntimeLibraryPaths.Add(BinaryLibraryFolder);
			PublicAdditionalLibraries.Add(DynLibPath);

			PublicDelayLoadDLLs.Add(LibraryFileName);
			RuntimeDependencies.Add(DynLibPath);

			if (Target.LinkType != TargetLinkType.Monolithic)
			{
				PublicSystemLibraries.Add("stdc++");
			}

			bWithFreeImage = true;
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			LibraryFileName = "libfreeimage-3.18.0.dylib";
			string DynLibPath = Path.Combine(BinaryLibraryFolder, LibraryFileName);

            PublicDelayLoadDLLs.Add(DynLibPath);   
			RuntimeDependencies.Add(DynLibPath);

			PublicSystemLibraries.Add("stdc++");
			bWithFreeImage = true;
		}

		PublicDefinitions.Add("WITH_FREEIMAGE_LIB=" + (bWithFreeImage ? '1' : '0'));
		if (LibraryFileName != "")
		{
			PublicDefinitions.Add("FREEIMAGE_LIB_FILENAME=\"" + LibraryFileName + "\"");
		}
	}
}
