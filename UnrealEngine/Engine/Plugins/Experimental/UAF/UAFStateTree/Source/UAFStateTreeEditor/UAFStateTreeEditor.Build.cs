// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UAFStateTreeEditor : ModuleRules
	{
		public UAFStateTreeEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				    "UAFStateTree",
				    "MessageLog",
				    "EditorInteractiveToolsFramework",
				    "InteractiveToolsFramework",
				    "EditorFramework",
				    "UAF",
				    "UnrealEd",
				    "AssetDefinition",
					"UAFAnimGraph",
				    "RigVM",
				    "StructUtilsEditor",
				    "PropertyEditor",
					"ToolMenus"
			    });

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"WorkspaceEditor",
					"StateTreeEditorModule",
					"StateTreeModule",
					"PropertyBindingUtils",
					"UAFStateTreeUncookedOnly",
					"UAFEditor",
					"SlateCore",
					"EditorSubsystem",
					"Engine",
					"Slate",
					"RigVMDeveloper",
					"UAFUncookedOnly"
				}
			);
		}
	}
}