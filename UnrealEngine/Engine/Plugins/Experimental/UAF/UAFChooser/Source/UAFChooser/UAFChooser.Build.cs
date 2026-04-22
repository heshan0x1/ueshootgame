// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFChooser : ModuleRules
	{
		public UAFChooser(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"RigVM",
					"ControlRig",
					"Engine",
					"UAF",
					"Chooser",
					"UAFAnimGraph",
				}
			);
		}
	}
}