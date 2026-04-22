// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MutableDataflowEditor : ModuleRules
	{
        public MutableDataflowEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add(ModuleDirectory + "/Private");
			PublicIncludePaths.Add(ModuleDirectory + "/Public");
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"CustomizableObject",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"DataflowCore",
					"DataflowEditor",
					"DataflowEngine",
					"DataflowNodes",
				});
		}
	}
}
