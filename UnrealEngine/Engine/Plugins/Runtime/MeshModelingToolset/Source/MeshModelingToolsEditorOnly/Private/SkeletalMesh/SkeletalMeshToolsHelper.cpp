// Copyright Epic Games, Inc. All Rights Reserved.


#include "SkeletalMesh/SkeletalMeshToolsHelper.h"

#include "BoneWeights.h"
#include "SkeletalMeshAttributes.h"
#include "Components/BaseDynamicMeshComponent.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "Async/ParallelFor.h"

void SkeletalMeshToolsHelper::GetUnposedMesh(
	TFunctionRef<void(FVertInfo, const FVector&)> WriteFunc,
	const FDynamicMesh3& PosedMesh,
	const FDynamicMesh3& SourceMesh,
	const TArray<FMatrix>& BoneMatrices,
	FName SkinWeightProfile,
	const TMap<FName, float>& MorphTargetWeights,
	const TArray<int32>& VertArray
	)
{

	using namespace UE::Geometry;
	using namespace UE::AnimationCore;

	SkinWeightProfile = SkinWeightProfile == NAME_None ? FSkeletalMeshAttributes::DefaultSkinWeightProfileName : SkinWeightProfile;
	FDynamicMeshVertexSkinWeightsAttribute* SkinWeightAttribute = SourceMesh.Attributes()->GetSkinWeightsAttribute(SkinWeightProfile);

	bool bHasVertSelection = VertArray.Num() > 0;
	int32 NumToProcess = bHasVertSelection ? VertArray.Num() : SourceMesh.MaxVertexID();
	
	ParallelFor(NumToProcess, [&](int32 Index)
		{
			int32 VertID = bHasVertSelection ? VertArray[Index] : Index;
			
			if (!SourceMesh.IsVertex(VertID))
			{
				return;
			}
				
			FBoneWeights BoneWeights;
			SkinWeightAttribute->GetValue(VertID, BoneWeights);
			
			FMatrix SkinMatrix = FMatrix(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector);
			SkinMatrix.M[3][3] = 0.0f;

			for (const FBoneWeight& BW : BoneWeights)
			{
				SkinMatrix += BoneMatrices[BW.GetBoneIndex()] * BW.GetWeight();
			}
			FVector PosedVertPos = PosedMesh.GetVertex(VertID);
				
			FVector UnposedVertPos = SkinMatrix.Inverse().TransformPosition(PosedVertPos);

			for (const TPair<FName, float>& MorphTargetWeight : MorphTargetWeights)
			{
				if (!FMath::IsNearlyZero(MorphTargetWeight.Value))
				{
					const FDynamicMeshMorphTargetAttribute* MorphTargetAttribute =
						SourceMesh.Attributes()->GetMorphTargetAttribute(MorphTargetWeight.Key);

					FVector Delta;
					MorphTargetAttribute->GetValue(VertID, Delta);
				
					UnposedVertPos += MorphTargetWeight.Value * Delta * (-1.0f);
				}
			}

			FVertInfo Info = {
				bHasVertSelection ? Index : INDEX_NONE,
				VertID,
			};
			
			WriteFunc(Info, UnposedVertPos);
		});
}

void SkeletalMeshToolsHelper::GetPosedMesh(
	TFunctionRef<void(int32, const FVector&)> WriteFunc, 
	const FDynamicMesh3& SourceMesh,
	const TArray<FMatrix>& BoneMatrices,
	FName SkinWeightProfile,
	const TMap<FName, float>& MorphTargetWeights
	)
{
	using namespace UE::Geometry;
	using namespace UE::AnimationCore;
	SkinWeightProfile = SkinWeightProfile == NAME_None ? FSkeletalMeshAttributes::DefaultSkinWeightProfileName : SkinWeightProfile;	
	FDynamicMeshVertexSkinWeightsAttribute* SkinWeightAttribute = SourceMesh.Attributes()->GetSkinWeightsAttribute(SkinWeightProfile);	
	ParallelFor(SourceMesh.MaxVertexID(), [&](int32 VertID)
		{
			if (!SourceMesh.IsVertex(VertID))
			{
				return;
			}
				
			FVector PosedVertPos = SourceMesh.GetVertex(VertID);
			for (const TPair<FName, float>& MorphTargetWeight : MorphTargetWeights)
			{
				if (!FMath::IsNearlyZero(MorphTargetWeight.Value))
				{
					const FDynamicMeshMorphTargetAttribute* MorphTargetAttribute =
						SourceMesh.Attributes()->GetMorphTargetAttribute(MorphTargetWeight.Key);

					FVector Delta;
					MorphTargetAttribute->GetValue(VertID, Delta);
			
					PosedVertPos += MorphTargetWeight.Value * Delta;
				}
			}	
				

			FBoneWeights BoneWeights;
			SkinWeightAttribute->GetValue(VertID, BoneWeights);
			
			FMatrix SkinMatrix = FMatrix(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector);
			SkinMatrix.M[3][3] = 0.0f;

			for (const FBoneWeight& BW : BoneWeights)
			{
				SkinMatrix += BoneMatrices[BW.GetBoneIndex()] * BW.GetWeight();
			}
			
			PosedVertPos = SkinMatrix.TransformPosition(PosedVertPos);

			WriteFunc(VertID, PosedVertPos);
		});
}

TArray<FMatrix> SkeletalMeshToolsHelper::ComputeBoneMatrices(const TArray<FTransform>& ComponentSpaceTransformsRefPose,
	const TArray<FTransform>& ComponentSpaceTransforms)
{
	check(ComponentSpaceTransformsRefPose.Num() == ComponentSpaceTransforms.Num());
	
	TArray<FMatrix> BoneMatrices;
	BoneMatrices.SetNumUninitialized(ComponentSpaceTransforms.Num());
	for (int32 BoneIndex = 0; BoneIndex < ComponentSpaceTransforms.Num(); ++BoneIndex)
	{
		BoneMatrices[BoneIndex] =
				ComponentSpaceTransformsRefPose[BoneIndex].ToMatrixWithScale().Inverse() *
				ComponentSpaceTransforms[BoneIndex].ToMatrixWithScale();
	}

	return BoneMatrices;
}

void SkeletalMeshToolsHelper::FPoseChangeDetector::CheckPose(const TArray<FTransform>& ComponentSpaceTransforms,
	const TMap<FName, float>& MorphTargetWeights)
{
	auto HasPoseChanged = [&]()
		{
			if (ComponentSpaceTransforms.Num() != PreviousComponentSpaceTransforms.Num())
			{
				return true;
			}
				
			for (int32 BoneIndex=0; BoneIndex< ComponentSpaceTransforms.Num(); ++BoneIndex)
			{
				const FTransform& CurrentBoneTransform = ComponentSpaceTransforms[BoneIndex];
				const FTransform& PrevBoneTransform = PreviousComponentSpaceTransforms[BoneIndex];
				if (!CurrentBoneTransform.Equals(PrevBoneTransform))
				{
					return true;
				}
			}
				
			if (!MorphTargetWeights.OrderIndependentCompareEqual(PreviousMorphTargetWeights))
			{
				return true;
			}
				
			return false;
		};

	const bool bPoseChanged = HasPoseChanged();
	bool bNotify = bPoseChanged;
	if (bPoseChanged)
	{
		if (State == PoseStoppedChanging)
		{
			State = PoseJustChanged;
		}
		else if (State == PoseJustChanged)
		{
			State = PoseChanged;
		}
	}
	else
	{
		if (State != PoseStoppedChanging)
		{
			State = PoseStoppedChanging;
			bNotify = true;
		}
	}

	if (bNotify)
	{
		if (PreviousComponentSpaceTransforms.IsEmpty())
		{
			PreviousComponentSpaceTransforms = ComponentSpaceTransforms;
			PreviousMorphTargetWeights = MorphTargetWeights;
		}
	
		FPayload Payload = {
			State,
			ComponentSpaceTransforms,
			MorphTargetWeights,
			PreviousComponentSpaceTransforms,
			PreviousMorphTargetWeights
		};
	
		Notifier.Broadcast(Payload);	
	}

	if (bPoseChanged)
	{
		PreviousComponentSpaceTransforms = ComponentSpaceTransforms;
		PreviousMorphTargetWeights = MorphTargetWeights;	
	}
}

SkeletalMeshToolsHelper::FPoseChangeDetector::FNotifier& SkeletalMeshToolsHelper::FPoseChangeDetector::GetNotifier() { return Notifier; }
