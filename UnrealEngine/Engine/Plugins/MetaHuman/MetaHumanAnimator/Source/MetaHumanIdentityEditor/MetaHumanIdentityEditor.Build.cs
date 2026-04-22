// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanIdentityEditor : ModuleRules
{
	public MetaHumanIdentityEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Projects",
			"Sequencer",
			"MediaAssets",
			"ImgMedia",
			"DesktopPlatform",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"ToolWidgets",
			"MovieScene",
			"GeometryFramework",
			"GeometryCore",
			"MeshConversion",
			"AdvancedPreviewScene",
			"InputCore",
			"ControlRigDeveloper",
			"RigVMDeveloper",
			"RigLogicModule",
			"AnimGraph",
			"ControlRig",
			"AssetDefinition",
			"SkeletalMeshDescription",
			"NNE",
			"ImageCore",
			"MetaHumanCore",
			"MetaHumanCoreEditor",
			"MetaHumanFaceContourTracker",
			"MetaHumanFaceFittingSolver",
			"MetaHumanImageViewerEditor",
			"MetaHumanCaptureData",
			"MetaHumanCaptureDataEditor",
			"MetaHumanPipeline",
			"MetaHumanSequencer",
			"MetaHumanIdentity",
			"MetaHumanToolkit",
			"MetaHumanPlatform",
			"CaptureDataCore",
			"MetaHumanCaptureUtils",
			"CaptureDataUtils",
			"MetaHumanCoreTechLib",
			"MetaHumanCoreTech",
			"MetaHumanSDKEditor"
		});
	}
}