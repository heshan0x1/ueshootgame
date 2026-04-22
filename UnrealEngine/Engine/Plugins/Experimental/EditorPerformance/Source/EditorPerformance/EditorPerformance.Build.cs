// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorPerformance : ModuleRules
{
	public EditorPerformance(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"ApplicationCore",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"EditorWidgets",
				"UnrealEd",
				"ToolMenus",
				"OutputLog",
				"EditorSubsystem",
				"WorkspaceMenuStructure",
				"MessageLog",
				"ToolWidgets",
				"DerivedDataCache",
				"Virtualization",
				"StudioTelemetry",
				"StallLogSubsystem"
			});

		PrivateIncludePathModuleNames.AddRange
		(
			new string[]
			{
				"WorkspaceMenuStructure",
			}
		);

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
