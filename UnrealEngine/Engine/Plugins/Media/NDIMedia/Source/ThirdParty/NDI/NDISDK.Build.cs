// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NDISDK : ModuleRules
{
    public NDISDK(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string IncludePath = Path.Combine(ModuleDirectory, "Include");
        PublicIncludePaths.Add(IncludePath);
        // The NDI SDK is available for Win64 + Linux, but this plugin only supports Win64
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string DllName = Path.Combine("Processing.NDI.Lib.x64.dll");
            string DllPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64", DllName);
            RuntimeDependencies.Add(DllPath);

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
        
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string DllName = Path.Combine("libndi.dylib");
            string DllPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Mac", DllName);
            RuntimeDependencies.Add(DllPath);

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
		
		if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            // Need to hardcode this to .6 since we don't sym link the shipping binaries
            string DllName = Path.Combine("libndi.so.6");
            string DllPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Linux", DllName);
            RuntimeDependencies.Add(DllPath);

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
    }
}
