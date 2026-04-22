// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AvfMedia : ModuleRules
	{
		public AvfMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AvfMediaFactory",
					"Core",
					"Engine",
					"ApplicationCore",
					"MediaUtils",
					"RenderCore",
					"RHI",
					"MetalRHI"
				});
			
			AddEngineThirdPartyPrivateStaticDependencies(Target, "MetalCPP");
			        
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PublicFrameworks.AddRange(
				new string[] {
					"CoreMedia",
					"CoreVideo",
					"AVFoundation",
					"AudioToolbox",
					"MediaToolbox",
					"QuartzCore"
				});
		}
	}
}
