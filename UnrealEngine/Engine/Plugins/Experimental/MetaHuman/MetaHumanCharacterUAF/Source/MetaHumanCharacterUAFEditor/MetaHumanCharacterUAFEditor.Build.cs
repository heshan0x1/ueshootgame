// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCharacterUAFEditor : ModuleRules
{
	public MetaHumanCharacterUAFEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"SubobjectDataInterface",
			"RigVMDeveloper",
			"UAF",
			"UAFAnimGraph",
			"UAFUncookedOnly",
			"MetaHumanCharacterEditor",
			"MetaHumanDefaultEditorPipeline",
			"MetaHumanCharacterPalette",
			"MetaHumanSDKRuntime"
		});
	}
}
