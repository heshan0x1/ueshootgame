// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class NiagaraNaniteEditor : ModuleRules
	{
		public NiagaraNaniteEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"Slate",
					"SlateCore",
					"InputCore",
					"RenderCore",
					"RHI",
					"EditorFramework",
					"UnrealEd",
					"AssetTools",
					"NiagaraNanite",
					"ToolMenus",
					"GraphEditor",
					"Niagara",
					"NiagaraEditor"
				}
			);
		}
	}
}
