// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCalibrationDiagnostics : ModuleRules
{
	public MetaHumanCalibrationDiagnostics(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "MHStereoCalibDiag";

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
			"OpenCVHelper",
			"OpenCV",
			"CaptureUtils",
			"MetaHumanImageViewer",
			"SequencerWidgets",
			"Projects",
			"InputCore",
			"OutputLog",
			"Json",
			"JsonUtilities",
			"WorkspaceMenuStructure"
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