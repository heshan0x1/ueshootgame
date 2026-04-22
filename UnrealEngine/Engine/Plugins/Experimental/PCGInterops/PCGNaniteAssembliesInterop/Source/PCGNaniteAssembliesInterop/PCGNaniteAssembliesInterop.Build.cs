// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PCGNaniteAssembliesInterop : ModuleRules
	{
		public PCGNaniteAssembliesInterop(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"PCG",
					"NaniteAssemblyEditorUtils",
				}
			);
		}
	}
}
