// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Wintab : ModuleRules
{
	public Wintab(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicIncludePaths.AddRange(
			[
				ModuleDirectory
			]
		);
	}
}
