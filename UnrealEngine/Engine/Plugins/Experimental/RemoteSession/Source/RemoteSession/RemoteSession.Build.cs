// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteSession : ModuleRules
{
	public RemoteSession(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MediaIOCore",
				"BackChannel",
				"ApplicationCore",
				"XRBase",
				// ... add other public dependencies that you statically link with here ...
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputDevice",
				"InputCore",
				"RHI",
				"Renderer",
				"RenderCore",
				"ImageWrapper",
				"MovieSceneCapture",
				"Sockets",
				"EngineSettings",
				"HeadMountedDisplay",
				"AugmentedReality",
				// iOS uses the Apple Image Utils plugin for GPU accellerated JPEG compression
				"AppleImageUtils"
			}
		);

		if (Target.bBuildEditor == true)
		{
			//reference the module "MyModule"
			PrivateDependencyModuleNames.Add("EditorFramework");
			PrivateDependencyModuleNames.Add("UnrealEd");

			//PrivateDependencyModuleNames.Add("PixelStreaming");
			//PrivateDependencyModuleNames.Add("PixelStreamingServers");
			//PrivateDependencyModuleNames.Add("PixelStreamingEditor");

			PrivateDependencyModuleNames.Add("PixelStreaming2");
			PrivateDependencyModuleNames.Add("PixelStreaming2Settings");
			PrivateDependencyModuleNames.Add("PixelStreaming2Servers");
			PrivateDependencyModuleNames.Add("PixelStreaming2Editor");
		}
	}
}
