// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MetaHumanCoreTechLib : ModuleRules
{
	public MetaHumanCoreTechLib(ReadOnlyTargetRules Target) : base(Target)
	{
		IWYUSupport = IWYUSupport.None;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Eigen",
			"RigLogicLib",
			"RigLogicModule",
			"MetaHumanSDKRuntime",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"Json",
			"ImageCore",
			"CaptureDataCore",
			"MetaHumanCoreTech",
			"simde"
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private/api"),
			Path.Combine(ModuleDirectory, "Private/bodyshapeeditor/include"),
			Path.Combine(ModuleDirectory, "Private/carbon/include"),
			Path.Combine(ModuleDirectory, "Private/nls/include"),
			Path.Combine(ModuleDirectory, "Private/nrr/include"),
			Path.Combine(ModuleDirectory, "Private/rig/include"),
			Path.Combine(ModuleDirectory, "Private/rigcalibration_core/include"),
			Path.Combine(ModuleDirectory, "Private/texture_synthesis/include"),
			Path.Combine(ModuleDirectory, "Private/rigmorpher/include"),
			Path.Combine(ModuleDirectory, "Private/conformer/include"),
		});

		PrivateDefinitions.Add("TITAN_DYNAMIC_API");
		PrivateDefinitions.Add("LOG_INTEGRATION");
		PrivateDefinitions.Add("EIGEN_MPL2_ONLY");
		PrivateDefinitions.Add("TITAN_NAMESPACE=coretechlib::epic::nls");
		PrivateDefinitions.Add("TITAN_API_NAMESPACE=coretechlib::titan::api");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDefinitions.Add("CARBON_ENABLE_SSE=1");
			PrivateDefinitions.Add("CARBON_ENABLE_AVX=0"); // CARBON_ENABLE_AVX does not work with Windows clang build
		}

		// This module uses exceptions in the core tech libs, so they must be enabled here
		bEnableExceptions = true;

		// AutoRTFM cannot be used with exceptions.
		bDisableAutoRTFMInstrumentation = true;
	}
	
}
