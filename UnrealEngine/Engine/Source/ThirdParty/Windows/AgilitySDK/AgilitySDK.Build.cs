// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System.IO;
using System.Linq;
using UnrealBuildBase;
using UnrealBuildTool;

public class AgilitySDK : ModuleRules
{
	public static string DefaultVersion = "1.616.1";

	private string GetAgilitySDKVersion()
	{
		ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, Target.BuildEnvironment == TargetBuildEnvironment.Unique ? DirectoryReference.FromFile(Target.ProjectFile) : null, Target.Platform);

		string AgilitySDKVersion = DefaultVersion;
		if (Ini.GetString("/Script/WindowsTargetPlatform.WindowsTargetSettings", "AgilitySDKVersion", out AgilitySDKVersion))
		{
			string AgilitySDKVersionPath = Path.Combine(ModuleDirectory, AgilitySDKVersion);
			if (!Path.Exists(AgilitySDKVersionPath))
			{
				Target.Logger.LogWarning($"AgilitySDK \"{AgilitySDKVersion}\" is not supported, falling back to \"{DefaultVersion}\"");
				AgilitySDKVersion = DefaultVersion;
			}
			else
			{
				Log.TraceInformationOnce($"Using configured AgilitySDK {AgilitySDKVersion}");
			}
		}
		else
		{
			AgilitySDKVersion = DefaultVersion;
		}

		return AgilitySDKVersion;
	}

	public AgilitySDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			string AgilitySDKVersion = GetAgilitySDKVersion();
			string AgilitySDKDir = Path.Combine(ModuleDirectory, AgilitySDKVersion);

			PublicDependencyModuleNames.Add("DirectX");

			// D3D12Core runtime
			PublicDefinitions.Add("D3D12_CORE_ENABLED=1");

			string BinarySrcPath = Path.Combine(AgilitySDKDir, "Binaries", Target.Architecture.WindowsLibDir);

			// Copy D3D12Core binaries to the target directory, so it can be found by D3D12.dll loader.
			// D3D redistributable search path is configured in LaunchWindows.cpp like so:
			// 		extern "C" { _declspec(dllexport) extern const UINT D3D12SDKVersion = 4; }
			// 		extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

			// NOTE: We intentionally put D3D12 redist binaries into a subdirectory.
			// System D3D12 loader will be able to pick them up using D3D12SDKPath export, if running on compatible Win10 version.
			// If we are running on incompatible/old system, we don't want those libraries to ever be loaded.
			// A specific D3D12Core.dll is only compatible with a matching d3d12SDKLayers.dll counterpart.
			// If a wrong d3d12SDKLayers.dll is present in PATH, it will be blindly loaded and the engine will crash.

			string[] DllsToLoad =
			[
				"D3D12Core.dll",
				"d3d12SDKLayers.dll"
			];

			foreach (string DllToLoad in DllsToLoad)
			{
				RuntimeDependencies.Add(
					$"$(TargetOutputDir)/D3D12/{Target.Architecture.WindowsLibDir}/{DllToLoad}",
					Path.Combine(BinarySrcPath, DllToLoad),
					StagedFileType.NonUFS
				);
			}

			PublicDefinitions.Add("D3D12_MAX_DEVICE_INTERFACE=12");
			PublicDefinitions.Add("D3D12_MAX_COMMANDLIST_INTERFACE=10");
			PublicDefinitions.Add("D3D12_MAX_FEATURE_OPTIONS=21");
			PublicDefinitions.Add("D3D12_SUPPORTS_INFO_QUEUE=1");
			PublicDefinitions.Add("D3D12_SUPPORTS_DXGI_DEBUG=1");
			PublicDefinitions.Add("D3D12_SUPPORTS_DEBUG_COMMAND_LIST=1");
			PublicDefinitions.Add("DXGI_MAX_FACTORY_INTERFACE=7");
			PublicDefinitions.Add("DXGI_MAX_SWAPCHAIN_INTERFACE=4");

			// Shared includes, mainly the linker helper
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

			PublicSystemIncludePaths.Add(Path.Combine(AgilitySDKDir, "Include"));

			// D3D 12 extensions
			PublicSystemIncludePaths.Add(Path.Combine(AgilitySDKDir, "Include", "d3dx12"));
		}
	}
}
