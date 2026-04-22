// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class GameInputWindowsLibrary : ModuleRules
	{
		public GameInputWindowsLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			Type = ModuleType.External;

			// This is the third party library provided by Microsoft for GameInput on Windows.
			// It comes from their NuGet repository: https://www.nuget.org/packages/Microsoft.GameInput/
			// Current version is: 0.2303.22621.3037

			// Game Input only supports x64 based platforms.
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) && Target.Architecture.bIsX64)
			{
				// Add the third party include folders so that we can include GameInput.h
				PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty"));

				// Add the GameInput.lib as a dependency.
				// This is located in Engine\Plugins\Runtime\GameInput\Source\GameInputWindowsLibrary\ThirdParty\Binaries\x64
				string LibPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "ThirdParty/Binaries/x64"));

				PublicAdditionalLibraries.Add(Path.Combine(LibPath, "GameInput.lib"));
			}
		}
	}
}
