// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StylusInputRealTimeStylus : ModuleRules
	{
		public StylusInputRealTimeStylus(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				[
					"Core",
					"Slate",
					"SlateCore",
					"StylusInput"
				]
			);
		}
	}
}