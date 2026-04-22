// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFStateTreeUncookedOnly : ModuleRules
	{
		public UAFStateTreeUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[] 
			{
				"StateTreeEditorModule",
				"PropertyBindingUtils",
				"RigVM",
				"RigVMDeveloper"
			});
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"StateTreeModule",
					"Engine",
					"WorkspaceEditor",
					"SlateCore",
					"UAF",
					"UAFUncookedOnly",
					"UAFStateTree",
					"UAFAnimGraph",
					"UAFAnimGraphUncookedOnly"
				}
			);
		}
	}
}