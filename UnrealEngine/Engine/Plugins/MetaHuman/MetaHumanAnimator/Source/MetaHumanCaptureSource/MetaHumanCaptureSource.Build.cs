// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCaptureSource : ModuleRules
{
	public MetaHumanCaptureSource(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"ImgMedia",
			"Engine",
			"MetaHumanCaptureUtils",
			"CaptureDataCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioEditor",
			"UnrealEd",
			"DesktopWidgets",
			"Slate",
			"SlateCore",
			"Json",
			"AssetDefinition",
			"CameraCalibrationCore",
			"OodleDataCompression",
			"MetaHumanCaptureData",
			"MetaHumanCore",
			"MetaHumanCoreEditor",
			"MetaHumanPipelineCore",
			"MetaHumanPipeline",
			"MetaHumanCaptureProtocolStack",
			"ImageWriteQueue",
			"RenderCore",
			"NNE",
			"MetaHumanFaceContourTracker",
			"InputCore",
			"CaptureDataUtils"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			//  Windows Media Foundation dependencies for the iPhone take data conversion
			//	Footage retrieval only works on Windows platforms for now.
			PublicDelayLoadDLLs.AddRange(new[] {
				"mf.dll", "mfplat.dll", "mfreadwrite.dll", "propsys.dll"
			});

			PublicSystemLibraries.AddRange(new[]
			{
				"mf.lib", "mfplat.lib", "mfreadwrite.lib", "mfuuid.lib", "propsys.lib"
			});
		}

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows)
			|| Target.Platform == UnrealTargetPlatform.Mac
			|| Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicDefinitions.Add("WITH_LIBJPEGTURBO=1");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "LibJpegTurbo");
		}
	}
}
