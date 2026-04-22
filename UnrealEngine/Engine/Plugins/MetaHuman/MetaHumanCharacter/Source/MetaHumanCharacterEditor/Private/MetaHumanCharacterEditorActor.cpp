// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSubsystem.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "MetaHumanCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Components/LODSyncComponent.h"
#include "Interfaces/IPluginManager.h"
#include "Animation/AnimInstance.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "SkelMeshDNAUtils.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanCharacterAnimInstance.h"
#include "RetargetComponent.h"
#include "Retargeter/IKRetargeter.h"

AMetaHumanCharacterEditorActor::AMetaHumanCharacterEditorActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BodyComponent = CreateDefaultSubobject<UDebugSkelMeshComponent>(TEXT("Body"));
	FaceComponent = CreateDefaultSubobject<UDebugSkelMeshComponent>(TEXT("Face"));
	LODSyncComponent = CreateDefaultSubobject<ULODSyncComponent>(TEXT("LODSync"));

	FaceComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	BodyComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);

	BodyComponent->SetupAttachment(RootComponent);
	FaceComponent->SetupAttachment(BodyComponent);

	// Add the IK retargeter component and assign our IK retargeter asset.
	static ConstructorHelpers::FObjectFinder<UIKRetargeter> IKRetargeterFinder(TEXT("/" UE_PLUGIN_NAME "/Animation/Retargeting/RTG_MH_IKRig"));
	if (IKRetargeterFinder.Succeeded())
	{
		IKRetargeter = IKRetargeterFinder.Object;
	}

	RetargetComponent = CreateDefaultSubobject<URetargetComponent>(TEXT("Retarget"));
}

void AMetaHumanCharacterEditorActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// PostRegisterAllComponents is called after all the components are initialized
	// ReinitAnimation will handle animation setup for this actor in the world
	// where it was spawned
	ReinitAnimation();
}

void AMetaHumanCharacterEditorActor::OnConstruction(const FTransform& InTransform)
{
	Super::OnConstruction(InTransform);

	// OnConstruction is called at the end of the construction script. This is here
	// to make sure animation is initialized in all cases where this actor can be used
	ReinitAnimation();
}

void AMetaHumanCharacterEditorActor::ReinitAnimation()
{
	if (!IsValid(FaceComponent) || !IsValid(BodyComponent) || !IsValid(Character))
	{
		return;
	}

	const bool bIsCharacterRigged = Character->HasFaceDNA();

	if (bIsCharacterRigged)
	{
		// TODO: Maybe define this at the blueprint level?
		TSubclassOf<UAnimInstance> FacePostProcessAnimBPClass = LoadClass<UAnimInstance>(this, TEXTVIEW("/" UE_PLUGIN_NAME "/Face/ABP_Face_PostProcess.ABP_Face_PostProcess_C"));
		TSubclassOf<UAnimInstance> BodyPostProcessAnimBPClass = LoadClass<UAnimInstance>(this, TEXTVIEW("/" UE_PLUGIN_NAME "/Body/ABP_Body_PostProcess.ABP_Body_PostProcess_C"));

		FaceComponent->SetOverridePostProcessAnimBP(FacePostProcessAnimBPClass);
		BodyComponent->SetOverridePostProcessAnimBP(BodyPostProcessAnimBPClass);

		switch (ActorDrivingAnimationMode)
		{
			case EMetaHumanActorDrivingAnimationMode::FromRetargetSource:
				RetargetComponent->SetForceOtherMeshesToFollowControlledMesh(false);
				RetargetComponent->SetRetargetAsset(IKRetargeter);
				RetargetComponent->SetControlledMesh(BodyComponent);
				break;

			case EMetaHumanActorDrivingAnimationMode::Manual:
				// The actor was likely spawned in the level so it will likely be animated using sequencer
				// ResetAnimation makes sure the actor can be driven directly from Animation Sequences
				FaceComponent->SetAnimInstanceClass(nullptr);
				BodyComponent->SetAnimInstanceClass(nullptr);

				RetargetComponent->SetRetargetAsset(nullptr);
				RetargetComponent->SetControlledMesh(nullptr);

				break;

			default:
				checkNoEntry();
		}
	}
	else
	{
		FaceComponent->SetOverridePostProcessAnimBP(nullptr);
		BodyComponent->SetOverridePostProcessAnimBP(nullptr);

		// Reset the state of the retarget component as there is no valid rig anymore
		RetargetComponent->SetRetargetAsset(nullptr);
		RetargetComponent->SetControlledMesh(nullptr);
	}

	Character->OnAnimationReinitialized.Broadcast();
}

void AMetaHumanCharacterEditorActor::ResetAnimation()
{
	// This function is not needed anymore as enabling/disabling animation is handled in OnRiggingStateChanged
	// But since this is needed as part of IMetaHumanCharacterEditorActorInterface, this body needs to be here until
	// the function can be removed from the interface
}

void AMetaHumanCharacterEditorActor::InitializeMetaHumanCharacterEditorActor(
	TNotNull<const UMetaHumanCharacterInstance*> InCharacterInstance,
	TNotNull<UMetaHumanCharacter*> InCharacter,
	TNotNull<USkeletalMesh*> InFaceMesh,
	TNotNull<USkeletalMesh*> InBodyMesh,
	int32 InNumLODs,
	const TArray<int32>& InFaceLODMapping,
	const TArray<int32>& InBodyLODMapping)
{
	CharacterInstance = InCharacterInstance;
	Character = InCharacter;
	FaceComponent->SetSkeletalMesh(InFaceMesh);
	BodyComponent->SetSkeletalMesh(InBodyMesh);

	{
		LODSyncComponent->NumLODs = InNumLODs;

		// Skeletal meshes
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Body"), ESyncOption::Drive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Face"), ESyncOption::Drive));

		// Grooms
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Hair"), ESyncOption::Passive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Eyebrows"), ESyncOption::Passive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Eyelashes"), ESyncOption::Passive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Mustache"), ESyncOption::Passive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Beard"), ESyncOption::Passive));
		LODSyncComponent->ComponentsToSync.Add(FComponentSync(TEXT("Peachfuzz"), ESyncOption::Passive));

		FLODMappingData& FaceMappingData = LODSyncComponent->CustomLODMapping.FindOrAdd(TEXT("Face"));
		FaceMappingData.Mapping = InFaceLODMapping;

		FLODMappingData& BodyMappingData = LODSyncComponent->CustomLODMapping.FindOrAdd(TEXT("Body"));
		BodyMappingData.Mapping = InBodyLODMapping;

		LODSyncComponent->RefreshSyncComponents();
	}

	InCharacter->OnRiggingStateChanged.AddUObject(this, &AMetaHumanCharacterEditorActor::OnRiggingStateChanged);
}

void AMetaHumanCharacterEditorActor::SetForcedLOD(int32 InForcedLOD)
{
	LODSyncComponent->ForcedLOD = InForcedLOD;
}

TNotNull<UMetaHumanCharacter*> AMetaHumanCharacterEditorActor::GetCharacter() const
{
	return Character;
}

TNotNull<const USkeletalMeshComponent*> AMetaHumanCharacterEditorActor::GetFaceComponent() const
{
	return FaceComponent;
}

TNotNull<const USkeletalMeshComponent*> AMetaHumanCharacterEditorActor::GetBodyComponent() const
{
	return BodyComponent;
}

void AMetaHumanCharacterEditorActor::OnFaceMeshUpdated()
{
	FaceComponent->MarkRenderStateDirty();
	FaceComponent->UpdateBounds();
}

void AMetaHumanCharacterEditorActor::OnBodyMeshUpdated()
{
	BodyComponent->MarkRenderStateDirty();
	BodyComponent->UpdateBounds();
}

void AMetaHumanCharacterEditorActor::SetActorDrivingAnimationMode(EMetaHumanActorDrivingAnimationMode InDrivingAnimationMode)
{
	if (ActorDrivingAnimationMode != InDrivingAnimationMode)
	{
		ActorDrivingAnimationMode = InDrivingAnimationMode;

		ReinitAnimation();
	}
}

void AMetaHumanCharacterEditorActor::SetDrivingSkeletalMesh(USkeletalMeshComponent* DrivingSkelMeshComponent)
{
	RetargetComponent->SetSourcePerformerMesh(DrivingSkelMeshComponent);

	if (IsValid(DrivingSkelMeshComponent))
	{
		SetActorDrivingAnimationMode(EMetaHumanActorDrivingAnimationMode::FromRetargetSource);
	}
	else
	{
		SetActorDrivingAnimationMode(EMetaHumanActorDrivingAnimationMode::Manual);
	}	
}

void AMetaHumanCharacterEditorActor::UpdateFaceComponentMesh(USkeletalMesh* InFaceMesh)
{
	if (InFaceMesh)
	{
		FaceComponent->SetSkeletalMesh(InFaceMesh);
	}
}

void AMetaHumanCharacterEditorActor::UpdateBodyComponentMesh(USkeletalMesh* InBodyMesh)
{
	if (InBodyMesh)
	{
		BodyComponent->SetSkeletalMesh(InBodyMesh);
	}
}

void AMetaHumanCharacterEditorActor::SetHairVisibilityState(EMetaHumanHairVisibilityState State)
{
	Blueprint_SetHairVisibilityState(State);
}

void AMetaHumanCharacterEditorActor::SetClothingVisibilityState(EMetaHumanClothingVisibilityState State, UMaterialInterface* OverrideMaterial)
{
	Blueprint_SetClothingVisibilityState(State, OverrideMaterial);
}

void AMetaHumanCharacterEditorActor::OnRiggingStateChanged()
{
	// Make sure the animation mode is up to date with the rigging state of the character
	ReinitAnimation();
}

void AMetaHumanCharacterEditorActor::SetShowNormalsOnFace(const bool InShowNormals)
{
	FaceComponent->bDrawNormals = InShowNormals;
	FaceComponent->MarkRenderStateDirty();
}

void AMetaHumanCharacterEditorActor::SetShowNormalsOnBody(const bool InShowNormals)
{
	BodyComponent->bDrawNormals = InShowNormals;
	BodyComponent->MarkRenderStateDirty();
}

void AMetaHumanCharacterEditorActor::SetShowTangentsOnFace(const bool InShowTangents)
{
	FaceComponent->bDrawTangents = InShowTangents;
	FaceComponent->MarkRenderStateDirty();
}

void AMetaHumanCharacterEditorActor::SetShowTangentsOnBody(const bool InShowTangents)
{
	BodyComponent->bDrawTangents = InShowTangents;
	BodyComponent->MarkRenderStateDirty();
}

void AMetaHumanCharacterEditorActor::SetShowBinormalsOnFace(const bool InShowBinormals)
{
	FaceComponent->bDrawBinormals = InShowBinormals;
	FaceComponent->MarkRenderStateDirty();
}

void AMetaHumanCharacterEditorActor::SetShowBinormalsOnBody(const bool InShowBinormals)
{
	BodyComponent->bDrawBinormals = InShowBinormals;
	BodyComponent->MarkRenderStateDirty();
}
