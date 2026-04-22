// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerSceneCapture.h"

#include "CompositeActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CompositeSceneCapture2DComponent.h"
#include "CompositeRenderTargetPool.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

UCompositeLayerSceneCapture::UCompositeLayerSceneCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bVisibleInSceneCaptureOnly{true} // By default, registered meshes will no longer be visible in the main render
	, bCustomRenderPass{false}
{
	Operation = ECompositeCoreMergeOp::Over;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// 5.7 hotfix: We must use delegates since adding virtual function overrides is not permitted
		FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UCompositeLayerSceneCapture::OnEndOfFrameUpdate);
#if WITH_EDITOR
		FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &UCompositeLayerSceneCapture::OnObjectPreEditChange);
#endif
	}
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
UCompositeLayerSceneCapture::~UCompositeLayerSceneCapture() = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UCompositeLayerSceneCapture::OnRemoved(const UWorld* World)
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	// Note: We don't use IsValid(..) since destruction should proceed even if the actor is pending kill.
	if (CompositeActor != nullptr)
	{
		CompositeActor->DestroySceneCaptures(this);
	}

	RestorePrimitiveVisibilityState();
}

void UCompositeLayerSceneCapture::BeginDestroy()
{
	Super::BeginDestroy();

	OnRemoved(GetWorld()); // Redundant remove call for safety

	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
#if WITH_EDITOR
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void UCompositeLayerSceneCapture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Actors))
	{
		SetActors(MoveTemp(Actors));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bCustomRenderPass))
	{
		SetCustomRenderPass(bCustomRenderPass);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bVisibleInSceneCaptureOnly))
	{
		SetVisibleInSceneCaptureOnly(bVisibleInSceneCaptureOnly);
	}
}

void UCompositeLayerSceneCapture::PreEditUndo()
{
	Super::PreEditUndo();

	RestorePrimitiveVisibilityState();
}

void UCompositeLayerSceneCapture::PostEditUndo()
{
	Super::PostEditUndo();

	SetActors(MoveTemp(Actors));

	SetCustomRenderPass(bCustomRenderPass);
}
#endif

bool UCompositeLayerSceneCapture::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return false;
	}

	USceneCaptureComponent2D* CaptureComponent = GetSceneCaptureComponent();
	if (!IsValid(CaptureComponent))
	{
		return false;
	}

	FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(CaptureComponent, CaptureComponent->TextureTarget, GetRenderResolution());

	FResourceMetadata Metadata = {};
	Metadata.bInvertedAlpha = true;
	Metadata.bDistorted = CaptureComponent->ShowFlags.LensDistortion;

	const ResourceId TexId = InContext.FindOrCreateExternalTexture(CaptureComponent->TextureTarget, Metadata);

	FPassInputDecl PassInput;
	PassInput.Set<FPassExternalResourceDesc>({ TexId });

	AddChildPasses(PassInput, InContext, InFrameAllocator, LayerPasses);

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0] = PassInput;
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("SceneCapture"), ELensDistortionHandling::Disabled);
	return true;
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerSceneCapture::GetActors() const
{
	return Actors;
}

void UCompositeLayerSceneCapture::SetActors(TArray<TSoftObjectPtr<AActor>> InActors)
{
	RestorePrimitiveVisibilityState();

	Actors = MoveTemp(InActors);

	UpdatePrimitiveVisibilityState();

	USceneCaptureComponent2D* SceneCaptureComponent = GetSceneCaptureComponent();
	if (IsValid(SceneCaptureComponent))
	{
		// First, we empty show-only actors since our previous logic used them
		SceneCaptureComponent->ShowOnlyActors.Empty();
		
		UpdateSceneCaptureComponents(*SceneCaptureComponent);
	}
}

bool UCompositeLayerSceneCapture::IsCustomRenderPass() const
{
	return bCustomRenderPass;
}

void UCompositeLayerSceneCapture::SetCustomRenderPass(bool bInIsFastRenderPass)
{
	bCustomRenderPass = bInIsFastRenderPass;

	UCompositeSceneCapture2DComponent* SceneCaptureComponent = GetSceneCaptureComponent();
	if (IsValid(SceneCaptureComponent))
	{
		if (bCustomRenderPass)
		{
			SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
			SceneCaptureComponent->bRenderInMainRenderer = true;
		}
		else
		{
			SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
			SceneCaptureComponent->bRenderInMainRenderer = false;
		}
	}
}

bool UCompositeLayerSceneCapture::IsVisibleInSceneCaptureOnly() const
{
	return bVisibleInSceneCaptureOnly;
}

void UCompositeLayerSceneCapture::SetVisibleInSceneCaptureOnly(bool bInVisible)
{
	bVisibleInSceneCaptureOnly = bInVisible;

	UpdatePrimitiveVisibilityState();
}

UCompositeSceneCapture2DComponent* UCompositeLayerSceneCapture::GetSceneCaptureComponent() const
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (IsValid(CompositeActor))
	{
		return CompositeActor->FindOrCreateSceneCapture<UCompositeSceneCapture2DComponent>(this);
	}

	return nullptr;
}

void UCompositeLayerSceneCapture::UpdatePrimitiveVisibilityState()
{
	for (TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					PrimitiveComponent->Modify();
					PrimitiveComponent->SetVisibleInSceneCaptureOnly(bVisibleInSceneCaptureOnly);
				}
			}
		}
	}
}

void UCompositeLayerSceneCapture::RestorePrimitiveVisibilityState()
{
	// First, we restore current actors to class default, in case they have been deleted.
	UPrimitiveComponent* CDO = UPrimitiveComponent::StaticClass()->GetDefaultObject<UPrimitiveComponent>();

	for (TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					PrimitiveComponent->Modify();
					PrimitiveComponent->SetVisibleInSceneCaptureOnly(CDO->bVisibleInSceneCaptureOnly);
				}
			}
		}
	}
}

void UCompositeLayerSceneCapture::UpdateSceneCaptureComponents(USceneCaptureComponent2D& SceneCaptureComponent) const
{
	SceneCaptureComponent.ShowOnlyComponents.Reset(Actors.Num());

	for (const TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (const AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					SceneCaptureComponent.ShowOnlyComponents.Add(PrimitiveComponent);
				}
			}
		}
	}
}

void UCompositeLayerSceneCapture::OnEndOfFrameUpdate(UWorld* InWorld)
{
	/**
	* In order to retain the ability to reference actors in sublevels, we prefer using weak pointer components on scene captures. However,
	* primitive components may often be recreated (see RerunConstructionScripts(), FStaticMeshComponentRecreateRenderStateContext, dynamic
	* text primitives, etc). To prevent issues with destroyed primitives disappearing in our scene captures, we re-register them every frame.
	*/
	if (IsEnabled())
	{
		ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();

		if (IsValid(CompositeActor) && CompositeActor->GetCompositeLayers().Contains(this))
		{
			USceneCaptureComponent2D* SceneCaptureComponent = GetSceneCaptureComponent();

			if (IsValid(SceneCaptureComponent))
			{
				UpdateSceneCaptureComponents(*SceneCaptureComponent);
			}
		}
	}
}

#if WITH_EDITOR
void UCompositeLayerSceneCapture::OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& EditPropertyChain)
{
	if (Object == this)
	{
		if (EditPropertyChain.GetActiveMemberNode())
		{
			const FProperty* Property = EditPropertyChain.GetActiveMemberNode()->GetValue();
			
			if (Property)
			{
				if (Property->GetName() == GET_MEMBER_NAME_CHECKED(UCompositeLayerSceneCapture, Actors))
				{
					RestorePrimitiveVisibilityState();
				}
			}
		}
	}
}
#endif

