// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanSpeech2Face : ModuleRules
{
	public MetaHumanSpeech2Face(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(new string[]
		{
            "Core",
            "ControlRig"
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
            "Json",
            "JsonUtilities",
            "NNE",
			"AudioPlatformConfiguration",
			"Projects",
			"Slate",
			"SlateCore",
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
			"AssetRegistry",
			"MetaHumanCore",
			"SignalProcessing",
			"MetaHumanCoreTech",
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ToolMenus",
				"ContentBrowser",
				"UnrealEd"
			});
		}
	}
}
