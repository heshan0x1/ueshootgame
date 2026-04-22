// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StylusInputMac : ModuleRules
	{
		public StylusInputMac(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				[
					"Core",
					"Slate",
					"SlateCore",
					"StylusInput"
				]
			);

			PublicFrameworks.AddRange(
				[
					"IOKit",
					"CoreFoundation",
					"Foundation"
				]
			);
		}
	}
}