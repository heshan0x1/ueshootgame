// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanDepthGenerator : ModuleRules
{
	public MetaHumanDepthGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(new string[]
		{
            "Core",
			"CoreUObject",
			"CaptureDataCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SlateCore",
			"Slate",
			"MetaHumanCore",
			"MetaHumanPipelineCore",
			"MetaHumanPipeline",
			"MetaHumanCaptureSource",
			"CameraCalibrationCore",
			"ImgMedia",
			"CaptureDataEditor",
			"CaptureDataUtils",
			"MetaHumanCaptureData",
			"MeshTrackerInterface"
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ToolMenus",
				"ToolWidgets",
				"ContentBrowser",
				"ContentBrowserData",
				"UnrealEd"
			});
		}
	}
}
