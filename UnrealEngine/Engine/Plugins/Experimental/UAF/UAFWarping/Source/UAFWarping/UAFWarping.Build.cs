// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFWarping : ModuleRules
	{
		public UAFWarping(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"AnimationCore",
					"RigVM",
					"Engine",
					"UAF",
					"UAFAnimGraph",
				}
			);
		}
	}
}