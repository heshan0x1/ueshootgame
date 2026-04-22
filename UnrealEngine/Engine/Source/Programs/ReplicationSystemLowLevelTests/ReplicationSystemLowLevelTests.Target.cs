// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class ReplicationSystemLowLevelTestsTarget : TestTargetRules
{
	public ReplicationSystemLowLevelTestsTarget(TargetInfo Target) : base(Target)
	{
		bCompileAgainstEngine = true;
		bCompileAgainstApplicationCore = true;
		bUsesSlate = false;

		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// Network config
		bWithPushModel = true;

		bEnableTrace = true;

		if (!bGenerateProjectFiles && (Target.Platform.IsInGroup(UnrealPlatformGroup.Linux) || Target.Platform == UnrealTargetPlatform.Win64))
		{
			//bUseAutoRTFMCompiler = true;
		}

		GlobalDefinitions.Add("UE_NET_TEST_FAKE_REP_TAGS=1");
		// Load time profiling brings object construction to a crawl.
		GlobalDefinitions.Add("LOADTIMEPROFILERTRACE_ENABLED=0");
		GlobalDefinitions.Add("UE_WITH_REMOTE_OBJECT_HANDLE=0");
	}
}
