// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Linq;
using UnrealBuildTool;

public class DX12 : ModuleRules
{
	public DX12(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PublicDependencyModuleNames.Add("DirectX");
			PublicDependencyModuleNames.Add("AgilitySDK");

			string[] AllD3DLibs = new string[]
			{
				"dxgi.lib",
				"d3d12.lib",
				"dxguid.lib",
			};

			string DirectXSDKDir = Target.WindowsPlatform.DirectXLibDir;
			PublicAdditionalLibraries.AddRange(AllD3DLibs.Select(LibName => Path.Combine(DirectXSDKDir, LibName)));

			// D3DX12 extensions, mainly the residency helper from DirectX-Graphics-Samples
			PublicSystemIncludePaths.Add(Path.Combine(Target.UEThirdPartySourceDirectory, "Windows", "D3DX12", "include"));
		}
	}
}
