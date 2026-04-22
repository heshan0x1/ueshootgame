// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFaceFittingSolverEditor : ModuleRules
{
	public MetaHumanFaceFittingSolverEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"UnrealEd",
			"AssetDefinition",
			"SlateCore",
			"Slate",
			"PropertyEditor",
			"MetaHumanFaceFittingSolver",
			"MetaHumanCoreEditor",
			"MetaHumanConfig",
			"MetaHumanConfigEditor",
		});
	}
}
