// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkUnrealDevice : ModuleRules
{
	public LiveLinkUnrealDevice(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"LiveLink",
				"LiveLinkDevice",
				"LiveLinkHub",
				"LiveLinkHubMessaging",
				"LiveLinkSequencer",
				"Messaging",
				"Networking",
				"Projects",
				"Slate",
				"SlateCore",
				"TakeRecorder",
			}
		);

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
