// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFStateTree : ModuleRules
	{
		public UAFStateTree(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UAFAnimGraph",
					"RigVM"
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"StateTreeModule",
					"Engine",
					"UAF",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UAFAnimGraph",
						"UAFAnimGraphUncookedOnly",
						"UAFEditor",
						"UAFUncookedOnly",
						"RigVMDeveloper",
						"StateTreeEditorModule",
						"UnrealEd"
					});
			}
		}
	}
}