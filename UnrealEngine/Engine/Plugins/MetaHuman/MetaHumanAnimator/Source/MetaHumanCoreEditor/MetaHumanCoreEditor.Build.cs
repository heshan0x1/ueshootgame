// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCoreEditor : ModuleRules
{
	public MetaHumanCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"UnrealEd",
			"Engine",
			"AssetDefinition",
			"RigLogicModule",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"InputCore",
			"MetaHumanCore",
			"CaptureDataCore",
			"CaptureDataEditor"
		});
	}
}
