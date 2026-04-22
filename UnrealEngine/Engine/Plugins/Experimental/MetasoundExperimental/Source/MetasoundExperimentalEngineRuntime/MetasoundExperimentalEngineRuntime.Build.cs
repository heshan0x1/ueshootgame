// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetasoundExperimentalEngineRuntime : ModuleRules
{
	public MetasoundExperimentalEngineRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MetasoundGraphCore",
				"MetasoundFrontend",
				"AudioExperimentalRuntime",
				"MetasoundExperimentalRuntime"
			}
		);
		
		// When the Editor builds all plugins, it will build with engine. In that case add a dependency on CoreUObject.
		// If we don't do this we will get linker errors for the Engine expecting to find stuff defined here.
		if (Target.bCompileAgainstEngine)
		{
			PublicDependencyModuleNames.Add("CoreUObject");
		}
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"MetasoundGraphCore",
				"MetasoundFrontend",
				"MetasoundEngine",
				"MetasoundStandardNodes",
				"SignalProcessing"
			}
		);

		PrivateDefinitions.AddRange(
			new string[]
			{
				"METASOUND_PLUGIN=MetasoundExperimental",
				"METASOUND_MODULE=MetasoundExperimentalEngineRuntime"
			}
		);
	}
}
