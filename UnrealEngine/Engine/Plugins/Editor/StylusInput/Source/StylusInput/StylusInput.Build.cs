// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StylusInput : ModuleRules
	{
		public StylusInput(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				[
					"Core",
					"CoreUObject",
					"EditorSubsystem",
					"Engine",
					"Slate",
					"UnrealEd",
				]
			);

			PrivateDependencyModuleNames.AddRange(
				[
					"SlateCore",
				]
			);
		}
	}
}
