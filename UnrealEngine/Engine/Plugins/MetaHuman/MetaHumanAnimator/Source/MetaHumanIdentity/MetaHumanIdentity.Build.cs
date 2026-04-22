// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanIdentity : ModuleRules
{
	public MetaHumanIdentity(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"SlateCore",
			"GeometryFramework",
			"MetaHumanCore",
			"MetaHumanCaptureData",
			"MetaHumanPipelineCore",
			"MetaHumanCoreTechLib"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"ImgMedia",
			"GeometryCore",
			"RigLogicModule",
			"MeshConversion",
			"MeshDescription",
			"SkeletalMeshDescription",
			"StaticMeshDescription",
			"Projects",
			"Json",
			"JsonUtilities",
			"MetaHumanFaceContourTracker",
			"MetaHumanFaceFittingSolver",
			"MetaHumanFaceAnimationSolver",
			"MetaHumanConfig",
			"MetaHumanPipeline",
			"CameraCalibrationCore",
			"InterchangeDNA",
			"CaptureDataCore",
			"MetaHumanCoreTech",
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("SkeletalMeshUtilitiesCommon");
			PrivateDependencyModuleNames.Add("Slate");
			PrivateDependencyModuleNames.Add("ControlRigDeveloper");
			PrivateDependencyModuleNames.Add("MetaHumanCaptureDataEditor");
			PublicDependencyModuleNames.Add("MetaHumanSDKEditor");
		}
	}
}