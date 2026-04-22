// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicSubmesh3.h"

#define UE_API MESHMODELINGTOOLSEDITORONLY_API

namespace UE::Geometry { class FDynamicMesh3; }

namespace SkeletalMeshToolsHelper
{
	using namespace UE::Geometry;

	struct FVertInfo
	{
		int32 VertArrayIndex;
		int32 VertID;
	};

	// Writes out the Mesh by first resetting the skeleton pose and then disable the morph targets
	// it can optionally take a Vert Array to only process a subset of verts
	UE_API void GetUnposedMesh(
		TFunctionRef<void(FVertInfo, const FVector&)> WriteFunc,
		const FDynamicMesh3& PosedMesh,
		const FDynamicMesh3& SourceMesh,
		const TArray<FMatrix>& BoneMatrices,
		FName SkinWeightProfile,
		const TMap<FName, float>& MorphTargetWeights,
		const TArray<int32>& VertArray = {}
		);

	
	UE_API void GetPosedMesh(
		TFunctionRef<void(int32, const FVector&)> WriteFunc,
		const FDynamicMesh3& SourceMesh,
		const TArray<FMatrix>& BoneMatrices,
		FName SkinWeightProfile,
		const TMap<FName, float>& MorphTargetWeights
		);
	
	UE_API TArray<FMatrix> ComputeBoneMatrices(
		const TArray<FTransform>& ComponentSpaceTransformsRefPose,
		const TArray<FTransform>& ComponentSpaceTransforms
		);

	struct FPoseChangeDetector
	{
		enum EState
		{
			PoseJustChanged,
			PoseChanged,
			PoseStoppedChanging
		};
		
		struct FPayload
		{
			EState CurrentState;
			const TArray<FTransform>& ComponentSpaceTransforms;
			const TMap<FName, float>& MorphTargetWeights;
			const TArray<FTransform>& PreviousComponentSpaceTransforms;
			const TMap<FName, float>& PreviousMorphTargetWeights;
		};

		DECLARE_MULTICAST_DELEGATE_OneParam(FNotifier, FPayload);	
		
		UE_API void CheckPose(const TArray<FTransform>& ComponentSpaceTransforms, const TMap<FName,float>& MorphTargetWeights);
		UE_API FNotifier& GetNotifier();
	
	protected:
		FNotifier Notifier;
	
		TArray<FTransform> PreviousComponentSpaceTransforms;
		TMap<FName, float> PreviousMorphTargetWeights;

		EState State = PoseStoppedChanging;
	};

}

#undef UE_API
