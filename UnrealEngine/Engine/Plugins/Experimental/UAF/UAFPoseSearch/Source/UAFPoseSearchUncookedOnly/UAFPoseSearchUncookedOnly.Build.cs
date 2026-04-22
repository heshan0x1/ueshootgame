// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFPoseSearchUncookedOnly : ModuleRules
	{
		public UAFPoseSearchUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"UAF",
					"UAFUncookedOnly",
					"UAFAnimGraph",
					"UAFAnimGraphUncookedOnly",
					"PoseSearch",
					"UAFPoseSearch",
					"RigVMDeveloper",
					"RigVM",
					"Slate",
					"SlateCore",
				}
			);
		}
	}
}