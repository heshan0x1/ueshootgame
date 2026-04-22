// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControlActorModifierBridge : ModuleRules
{
	public RemoteControlActorModifierBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ActorModifierCore",
				"Core",
				"CoreUObject",
				"OperatorStackEditor",
				"PropertyEditor",
				"PropertyAnimatorCore",
				"RemoteControl",
				"RemoteControlUI"
			}
		);
	}
}