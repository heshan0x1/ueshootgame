// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFControlRigEditor : ModuleRules
	{
		public UAFControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"Core",
					"ControlRig",
					"ControlRigDeveloper",
					"RigVM",
					"UAF",
					"UAFAnimGraph",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"RigVMDeveloper",
					"WorkspaceEditor",
					"SlateCore",
					"Slate",
					"RigVMEditor",
					"UAFEditor",
					"UAFControlRig",
					"UAFUncookedOnly",
					"UAFAnimGraphUncookedOnly",
				}
			);
		}
	}
}
