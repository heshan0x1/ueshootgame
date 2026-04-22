// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFAnimGraphUncookedOnly : ModuleRules
	{
		public UAFAnimGraphUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"SlateCore"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UAF",
					"UAFUncookedOnly",
					"UAFAnimGraph",
					"RigVMDeveloper",
					"BlueprintGraph",	// For K2 schema
					"AnimGraph",
					"RigVM",
					"Slate",
					"AssetDefinition",
					"Projects",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"BlueprintGraph",
						"WorkspaceEditor",
						"UAFEditor",
						"RigVMEditor",
					}
				);
			}
		}
	}
}