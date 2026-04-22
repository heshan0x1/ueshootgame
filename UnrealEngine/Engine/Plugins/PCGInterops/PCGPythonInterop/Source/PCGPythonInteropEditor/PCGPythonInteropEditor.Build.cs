// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PCGPythonInteropEditor : ModuleRules
	{
		public PCGPythonInteropEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"Projects",
					"PCG"
				});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"AssetDefinition",
						"PCGEditor",
						"UnrealEd",
						"PythonScriptPlugin",
						"Slate"
					}
				);
			}
		}
	}
}
