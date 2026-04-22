// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StylusInputWintab : ModuleRules
	{
		public StylusInputWintab(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				[
					"Core",
					"Slate",
					"SlateCore",
					"StylusInput",
					"Wintab"
				]
			);
		}
	}
}