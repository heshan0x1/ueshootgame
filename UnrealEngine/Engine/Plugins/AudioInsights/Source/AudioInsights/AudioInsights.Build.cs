// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioInsights : ModuleRules
{
	public AudioInsights(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core",
				"RewindDebuggerRuntimeInterface",
				"TraceInsights",
				"TraceServices",
				"ToolWidgets",
			}
		);	
		
		PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
				"AudioMixerCore",
				"AudioWidgetsCore",
				"CoreUObject",
				"InputCore",
				"OutputLog",
				"SessionServices",
				"Slate",
				"SlateCore",
				"Sockets",
				"TraceAnalysis",
				"TraceLog",
				"ToolMenus",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"UnrealEd",
			});
		}
	}
}
