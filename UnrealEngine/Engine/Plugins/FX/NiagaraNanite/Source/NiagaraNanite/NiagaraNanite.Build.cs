// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NiagaraNanite : ModuleRules
	{
		public NiagaraNanite(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(new[] { "Core" });

			PrivateDependencyModuleNames.AddRange(
				new[]
				{
					"CoreUObject",
					"Engine",
					"Renderer",
					"Slate",
					"SlateCore",
					"Projects",
				
					// Data interface dependencies
					"Niagara",
					"NiagaraCore",
					"NiagaraNaniteShader",
					"VectorVM",
					"RenderCore",
					"RHI"
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}
		}
	}
}
