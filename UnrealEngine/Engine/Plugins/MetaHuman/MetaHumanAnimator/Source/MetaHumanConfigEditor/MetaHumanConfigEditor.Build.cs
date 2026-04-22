// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanConfigEditor : ModuleRules
{
	public MetaHumanConfigEditor(ReadOnlyTargetRules Target) : base(Target)
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
			"InputCore",
			"MetaHumanConfig",
			"MetaHumanCoreEditor",
		});
	}
}
