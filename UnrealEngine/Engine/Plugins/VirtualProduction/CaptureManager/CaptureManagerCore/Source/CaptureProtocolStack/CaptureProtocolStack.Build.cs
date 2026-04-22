// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CaptureProtocolStack : ModuleRules
{
	public CaptureProtocolStack(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(new string[]
		{
            "Core",
			"CaptureUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Json",
            "JsonUtilities",
			"Networking",
			"Sockets"
		});
	}
}
