// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFPoseSearch : ModuleRules
	{
		public UAFPoseSearch(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"UAF",
					"UAFAnimGraph",
					"Core",
					"CoreUObject",
					"Engine",
					"HierarchyTableRuntime",
					"HierarchyTableAnimationRuntime",
					"PoseSearch",
					"Chooser",
					"RigVM",
				}
			);
		}
	}
}