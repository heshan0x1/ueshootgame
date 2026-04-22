// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class AudioSynesthesiaCore : ModuleRules
	{
        public AudioSynesthesiaCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePathModuleNames.AddRange(
				new string[] {
					"AudioAnalyzer"
				}
			);
			
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"SignalProcessing",
                }
            );

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AudioMixerCore"
				}
			);
		}
	}
}
