// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class MetaHumanFaceContourTrackerEditor : ModuleRules
{
	public MetaHumanFaceContourTrackerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"UnrealEd",
			"AssetDefinition",
			"MetaHumanCoreEditor",
			"MetaHumanFaceContourTracker",
			"MetaHumanCore",
		});
	}
}