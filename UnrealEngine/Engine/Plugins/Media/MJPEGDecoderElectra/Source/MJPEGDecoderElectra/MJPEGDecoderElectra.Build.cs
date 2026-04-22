// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MJPEGDecoderElectra : ModuleRules
	{
		public MJPEGDecoderElectra(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
                    "Projects",
                    "ElectraBase",
                    "ElectraSamples",
					"ElectraCodecFactory",
					"ElectraDecoders"
                });

            if (Target.Platform == UnrealTargetPlatform.Win64)// || Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)
            {
                DynamicallyLoadedModuleNames.AddRange(
                    new string[] {
                    "ImageWrapper",
                    });

                PrivateIncludePathModuleNames.AddRange(
                    new string[] {
                    "ImageWrapper",
                    });
			}
		}
	}
}
