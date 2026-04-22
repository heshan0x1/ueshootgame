// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFAnimGraphTestSuite : ModuleRules
	{
		public UAFAnimGraphTestSuite(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UAF",
					"UAFTestSuite",
					"UAFAnimGraph",
					"RigVM", 
					"PythonScriptPlugin",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"UAFUncookedOnly",
						"UAFEditor",
						"RigVMDeveloper",
						"UAFAnimGraphEditor",
						"UAFAnimGraphUncookedOnly",
					}
				);
			}
		}
	}
}