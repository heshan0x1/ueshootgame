// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimGraphViewportController.h"

#include "AdvancedPreviewScene.h"
#include "AnimNextAnimGraphWorkspaceAssetUserData.h"
#include "Component/AnimNextComponent.h"
#include "UAF/Viewport/ViewportSceneDescription.h"
#include "Engine/PreviewMeshCollection.h"
#include "Components/SkeletalMeshComponent.h"

namespace UE::UAF::Editor
{
	void FAnimGraphViewportController::OnEnter(const FViewportContext& InViewportContext)
	{
		UAnimNextAnimationGraph* Graph = CastChecked<UAnimNextAnimationGraph>(InViewportContext.OutlinerObject);
		UUAFViewportSceneDescription* UAFViewportSceneDescription = CastChecked<UUAFViewportSceneDescription>(InViewportContext.SceneDescription);

		// TODO: Consider building this module programatically
		TObjectPtr<UAnimNextModule> Module = LoadObject<UAnimNextModule>(GetTransientPackage(), TEXT("/UAFAnimGraph/Internal/S_SingleGraph.S_SingleGraph"));
		
		const TObjectPtr<UWorld> PreviewWorld = InViewportContext.PreviewScene->GetWorld();

		const auto AddActor = [&](USkeletalMesh* InSkeletalMesh)
			{
				AActor* PreviewActor = PreviewWorld->SpawnActor<AActor>(AActor::StaticClass());
				check(PreviewActor);

				USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(PreviewActor);
				InViewportContext.PreviewScene->AddComponent(SkeletalMeshComponent, FTransform::Identity);
				PreviewActor->SetRootComponent(SkeletalMeshComponent);
				SkeletalMeshComponent->SetEnableAnimation(false);
				SkeletalMeshComponent->SetSkeletalMesh(InSkeletalMesh);

				UAnimNextComponent* UAFComponent = NewObject<UAnimNextComponent>(PreviewActor);
				PreviewActor->AddInstanceComponent(UAFComponent);
				UAFComponent->SetModule(Module);
				UAFComponent->RegisterComponent();
				UAFComponent->InitializeComponent();
		
				FAnimNextVariableReference VariableReference("Graph", Module);
				check(UAFComponent->SetVariable<TObjectPtr<UObject>>(VariableReference, Graph));

				PreviewActors.Add(PreviewActor);
			};

		if (UAFViewportSceneDescription->SkeletalMesh)
		{
			AddActor(UAFViewportSceneDescription->SkeletalMesh);
		}

		if (UAFViewportSceneDescription->AdditionalMeshes)
		{
			for (int32 MeshIndex = 0; MeshIndex < UAFViewportSceneDescription->AdditionalMeshes->SkeletalMeshes.Num(); ++MeshIndex)
			{
				const TSoftObjectPtr<USkeletalMesh> SkelMeshSoftPtr = UAFViewportSceneDescription->AdditionalMeshes->SkeletalMeshes[MeshIndex].SkeletalMesh;
				
				if (!SkelMeshSoftPtr.IsNull())
				{
					USkeletalMesh* SkeletalMesh = SkelMeshSoftPtr.LoadSynchronous();
					if (SkeletalMesh)
					{
						AddActor(SkeletalMesh);
					}
				}
			}
		}
	}
	
	void FAnimGraphViewportController::OnExit()
	{
		for (AActor* PreviewActor : PreviewActors)
		{
			PreviewActor->Destroy();
			PreviewActor = nullptr;
		}
		
		PreviewActors.Empty();
	}
}
