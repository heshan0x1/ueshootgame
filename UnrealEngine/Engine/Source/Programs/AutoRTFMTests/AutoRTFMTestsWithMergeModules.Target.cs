// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;
using System.Collections.Generic;
using UnrealBuildBase;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class AutoRTFMTestsWithMergeModulesTarget : AutoRTFMTestsTarget
{
	public AutoRTFMTestsWithMergeModulesTarget(TargetInfo Target) : base(Target)
	{
		bMergeModules = true;
	}
}
