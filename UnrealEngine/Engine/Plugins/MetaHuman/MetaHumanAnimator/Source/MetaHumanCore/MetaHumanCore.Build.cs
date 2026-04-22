// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCore : ModuleRules
{
	public MetaHumanCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RigLogicLib",
			"RigLogicModule",
			"CaptureDataCore",
			"MetaHumanCoreTech",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"Projects",
			"ImgMedia",
			"Json",
			"JsonUtilities",
			"CameraCalibrationCore",
			"RHI",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PrivateDefinitions.Add("CORE_BUILD_SHARED");
	}
}
