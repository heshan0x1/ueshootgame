// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCalibrationGenerator : ModuleRules
{
	public MetaHumanCalibrationGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "MHCalibGenerator";

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"CaptureDataCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"MetaHumanCalibrationLib",
			"MetaHumanCalibrationCore",
			"CaptureDataEditor",
			"CaptureDataUtils",
			"SlateCore",
			"Slate",
			"ImageWrapper",
			"ImgMedia",
			"ImageCore",
			"CaptureUtils",
			"MetaHumanImageViewer",
			"SequencerWidgets",
			"Projects",
			"InputCore",
			"OutputLog",
			"Json",
			"JsonUtilities",
			"WorkspaceMenuStructure",
			"SettingsEditor",
			"OpenCVHelper",
			"OpenCV"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ToolMenus",
				"ToolWidgets",
				"ContentBrowser",
				"UnrealEd"
			});
		}
	}
}