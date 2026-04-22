// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class VideoLiveLinkDeviceCommon : ModuleRules
{
	public VideoLiveLinkDeviceCommon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		ShortName = "VideoLLDCommon";

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"CaptureManagerTakeMetadata",
			"ImageCore",
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CaptureManagerMediaRW",
			"CaptureManagerSettings",
			"Engine",
			"Slate",
			"SlateCore",
			"CoreUObject",
			"UnrealEd"
		});
	}
}
