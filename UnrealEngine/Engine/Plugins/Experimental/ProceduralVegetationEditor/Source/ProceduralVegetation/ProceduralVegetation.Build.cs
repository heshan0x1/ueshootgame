// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ProceduralVegetation : ModuleRules
	{
		public ProceduralVegetation(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"DynamicWind"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
					"Engine",
					"RenderCore",
					"GeometryCore",
					"GeometryFramework",
					"Json",
					"JsonUtilities",
					"PCG",
				}
			);
		}
	}
}
