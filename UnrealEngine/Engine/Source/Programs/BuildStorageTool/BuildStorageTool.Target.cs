// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class BuildStorageToolTarget : TargetRules
{
	public BuildStorageToolTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		LaunchModuleName = "BuildStorageTool";

		bUseXGEController				= false;
		bLoggingToMemoryEnabled			= true;
		bUseLoggingInShipping			= true;
		bCompileWithAccessibilitySupport= false;
		bWithServerCode					= false;
		bCompileNavmeshClusterLinks		= false;
		bCompileNavmeshSegmentLinks		= false;
		bCompileRecast					= false;
		bCompileICU 					= true; // Required for regex support
		bWithLiveCoding					= false;
		bBuildDeveloperTools			= false;
		bBuildWithEditorOnlyData		= false;
		bCompileAgainstEngine			= false;
		bCompileAgainstCoreUObject		= true;
		bCompileAgainstApplicationCore	= true;
		bUsesSlate						= true;
		bForceDisableAutomationTests	= true;

		bIsBuildingConsoleApplication = false;

		WindowsPlatform.bUseBundledDbgHelp = false;
		WindowsPlatform.bPixProfilingEnabled = false;
		WindowsPlatform.bMergeIdenticalCOMDATs = true;
		WindowsPlatform.bStripUnreferencedSymbols = true;

		GlobalDefinitions.Add("WITH_AUTOMATION_TESTS=0");
		GlobalDefinitions.Add("WITH_AUTOMATION_WORKER=0");
	}
}
