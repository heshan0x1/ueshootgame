// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PluginUtils : ModuleRules
	{
		public PluginUtils(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Projects",
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AssetRegistry",
					"AssetTools",
					"CoreUObject",
					"DesktopPlatform",
					"EditorScriptingUtilities",
					"Engine",
					"GameProjectGeneration",
					"GameplayTags",
					"SourceControl",
					"SubobjectDataInterface",
					"UnrealEd"
				}
			);
		}
	}
}
