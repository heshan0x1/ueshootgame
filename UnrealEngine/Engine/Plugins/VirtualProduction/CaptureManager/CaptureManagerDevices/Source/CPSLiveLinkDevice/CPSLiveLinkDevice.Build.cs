// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CPSLiveLinkDevice : ModuleRules
{
	public CPSLiveLinkDevice(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		ShortName = "CPSLLD";

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"CaptureUtils",
			"CaptureProtocolStack",
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"Json",
			"CaptureManagerMediaRW",
			"DataIngestCore",
			"IngestLiveLinkDevice",
			"LiveLinkDevice",
			"LiveLinkCapabilities",
			"Media",
			"LiveLinkHubCaptureMessaging",
			"LiveLinkHub",
			"LiveLinkFaceMetadata",
			"ToolWidgets",
			"Slate",
			"SlateCore",
			"StereoCameraMetadata",
			"CaptureManagerTakeMetadata",
			"CaptureManagerSettings",
			"CoreUObject",
			"UnrealEd"
		});

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
