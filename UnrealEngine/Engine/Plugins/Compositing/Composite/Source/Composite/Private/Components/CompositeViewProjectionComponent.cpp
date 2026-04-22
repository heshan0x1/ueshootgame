// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/CompositeViewProjectionComponent.h"
#include "CompositeActor.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "CompositeCoreSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Composite"

UCompositeViewProjectionComponent::UCompositeViewProjectionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> DefaultMPC = TEXT("/Composite/Materials/MPC_Composite.MPC_Composite");

	MaterialParameterCollection = DefaultMPC.Object;
	MatrixParameterName = TEXT("CameraViewProjectionMatrix");

	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bTickInEditor = true;
	bAutoActivate = true;
}

void UCompositeViewProjectionComponent::ForceUpdate()
{
	LastViewProjectionMatrix = FMatrix::Identity;
}

FComponentReference& UCompositeViewProjectionComponent::GetTargetComponent()
{
	return TargetCameraComponent;
}

void UCompositeViewProjectionComponent::SetTargetComponent(const FComponentReference& InComponentReference)
{
	TargetCameraComponent = InComponentReference;
	LastCameraComponent = Cast<UCameraComponent>(TargetCameraComponent.GetComponent(nullptr));

	ForceUpdate();
}

void UCompositeViewProjectionComponent::InitDefaultCamera()
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (IsValid(CompositeActor))
	{
		// Special case when the component is owned by the composite actor: we use its camera component reference directly.
		SetTargetComponent(CompositeActor->GetCamera());

		return;
	}

	UCameraComponent* TargetCamera = Cast<UCameraComponent>(TargetCameraComponent.GetComponent(GetOwner()));
	
	if (!IsValid(TargetCamera))
	{
		// Find all CineCameraComponents on the same actor as this component and set the first one to be the target
		TInlineComponentArray<UCameraComponent*> CameraComponents;
		if (AActor* Owner = GetOwner())
		{
			Owner->GetComponents(CameraComponents);
		}

		if (CameraComponents.Num() > 0)
		{
			UCameraComponent* TargetComponent = CameraComponents[0];
			TargetCameraComponent.ComponentProperty = TargetComponent->GetFName();
			LastCameraComponent = TargetComponent;
		}
	}
}

void UCompositeViewProjectionComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

void UCompositeViewProjectionComponent::OnRegister()
{
	Super::OnRegister();

	InitDefaultCamera();

	/*
	* NOTE: We must (unfortunately) rely on this after-tick event to update the view matrix projection,
	* since viewport camera transform updates after ticking all world objects.
	*
	* The CompositePlane plugin updated this information on tick, but a lag becomes visible when
	* piloting the source projection camera for example.
	*/
	OnWorldPreSendAllEndOfFrameUpdatesHandle = FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UCompositeViewProjectionComponent::UpdateProjection);
}

void UCompositeViewProjectionComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	InitDefaultCamera();
}

void UCompositeViewProjectionComponent::PostEditImport()
{
	Super::PostEditImport();

	InitDefaultCamera();
}

void UCompositeViewProjectionComponent::PostLoad()
{
	Super::PostLoad();
}

void UCompositeViewProjectionComponent::OnUnregister()
{
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.Remove(OnWorldPreSendAllEndOfFrameUpdatesHandle);

	Super::OnUnregister();
}

void UCompositeViewProjectionComponent::UpdateProjection(UWorld* InWorld) const
{
	if (!IsValid(InWorld))
	{
		return;
	}

	if (!bIsEnabled)
	{
		return;
	}

	const ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (IsValid(CompositeActor) && !CompositeActor->IsRendering())
	{
		// Since the component can be used without the composite actor, we only stop updates when it is valid but not rendering.
		return;
	}

	UMaterialParameterCollection* MPC = MaterialParameterCollection.Get();

	if (IsValid(LastCameraComponent) && IsValid(MPC))
	{
		FMinimalViewInfo DesiredView;
		FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;

		LastCameraComponent->GetCameraView(FApp::GetDeltaTime(), DesiredView);

		UGameplayStatics::GetViewProjectionMatrix(DesiredView, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);

		if (!LastViewProjectionMatrix.Equals(ViewProjectionMatrix))
		{
			int32 ParameterProjection = INDEX_NONE;
			for (int32 ParameterIndex = 0; ParameterIndex < MPC->VectorParameters.Num(); ParameterIndex++)
			{
				FName CurrentParameterName = MPC->VectorParameters[ParameterIndex].ParameterName;
				if (!CurrentParameterName.IsNone())
				{
					if (CurrentParameterName == MatrixParameterName)
					{
						ParameterProjection = ParameterIndex;
					}
				}
			}

			UMaterialParameterCollectionInstance* MaterialParameterCollectionInstance = GetWorld()->GetParameterCollectionInstance(MaterialParameterCollection);
			// Ensure there's space for 4 output vectors for the projection matrix
			if (ParameterProjection != INDEX_NONE && ParameterProjection + 4 <= MaterialParameterCollection->VectorParameters.Num())
			{
				FMatrix44f ViewProjectionMatrixf = FMatrix44f(ViewProjectionMatrix);

				// Store the vectors to the collection instance
				const FLinearColor* MatrixVectors = (const FLinearColor*)&ViewProjectionMatrixf;
				for (int32 ElementIndex = 0; ElementIndex < 4; ElementIndex++)
				{
					MaterialParameterCollectionInstance->SetVectorParameterValue(MaterialParameterCollection->VectorParameters[ParameterProjection + ElementIndex].ParameterName, MatrixVectors[ElementIndex]);
				}

				MaterialParameterCollectionInstance->UpdateRenderState(false);
			}

			LastViewProjectionMatrix = ViewProjectionMatrix;
		}
	}
}

void UCompositeViewProjectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MarkForNeededEndOfFrameUpdate();
}

#if WITH_EDITOR
void UCompositeViewProjectionComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, TargetCameraComponent))
	{
		LastCameraComponent = Cast<UCameraComponent>(TargetCameraComponent.GetComponent(GetOwner()));
	}

	// Force MPC updates on any property change.
	ForceUpdate();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
