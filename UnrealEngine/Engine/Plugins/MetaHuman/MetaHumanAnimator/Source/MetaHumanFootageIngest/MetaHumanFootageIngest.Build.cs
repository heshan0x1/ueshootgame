// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanFootageIngest : ModuleRules
{
	public MetaHumanFootageIngest(ReadOnlyTargetRules Target) : base(Target)
	{
		bool bIsUEFN = ((Target.Name == "UnrealEditorFortnite") || (Target.Name == "FortniteContentWorker"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"ContentBrowserData",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"InputCore",
			"MetaHumanCore",
			"ToolMenus",
			"ToolWidgets",
			"EditorStyle",
			"Projects",
			"PropertyEditor",
			"ImgMedia",
			"Json",
			"Sockets",
			"Networking",
			"GeometryCore",
			"GeometryFramework",
			"MeshDescription",
			"StaticMeshDescription",
			"MetaHumanCaptureSource",
			"MetaHumanCaptureData",
			"MetaHumanCaptureUtils",
			"MetaHumanCoreEditor",
			"CaptureDataCore",
			"MeshTrackerInterface",
			"WorkspaceMenuStructure",
			"CaptureDataUtils",
			"LauncherPlatform",
			"LiveLinkHubEditor"
		});

		PrivateDefinitions.Add("HIDE_MAIN_MENU");

		if (!bIsUEFN)
		{
			PrivateDefinitions.Add("SHOW_CAPTURE_SOURCE_FILTER");
		}
	}
}