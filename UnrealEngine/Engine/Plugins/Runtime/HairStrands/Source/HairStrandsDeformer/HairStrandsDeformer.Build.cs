// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
namespace UnrealBuildTool.Rules
{
	public class HairStrandsDeformer : ModuleRules
	{
		public HairStrandsDeformer(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"NiagaraCore",
					"Niagara",
					"RenderCore",
					"Renderer",
					"RHI",
					"ComputeFramework",
					"OptimusCore",
					"HairStrandsCore",
					"HairStrandsSolver", 
					"Chaos",
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"Shaders",
				});
			
			PrivateIncludePaths.AddRange(
				new string[] {
					Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Private"),
				}
			);
		}
	}
}
