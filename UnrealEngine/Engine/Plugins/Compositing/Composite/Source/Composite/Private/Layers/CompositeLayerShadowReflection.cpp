// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerShadowReflection.h"

#include "CompositeActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CompositeShadowReflectionCatcherComponent.h"
#include "CompositeRenderTargetPool.h"
#include "Containers/StaticArray.h"

UCompositeLayerShadowReflection::UCompositeLayerShadowReflection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Operation = ECompositeCoreMergeOp::Multiply;
	AutoConfigureActors = ECompositeHiddenInSceneCaptureConfiguration::None;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// 5.7 hotfix: We must use delegates since adding virtual function overrides is not permitted
		FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UCompositeLayerShadowReflection::OnEndOfFrameUpdate);
#if WITH_EDITOR
		FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddUObject(this, &UCompositeLayerShadowReflection::OnObjectPreEditChange);
#endif
	}
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
UCompositeLayerShadowReflection::~UCompositeLayerShadowReflection() = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UCompositeLayerShadowReflection::OnRemoved(const UWorld* World)
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	// Note: We don't use IsValid(..) since destruction should proceed even if the actor is pending kill.
	if (CompositeActor != nullptr)
	{
		CompositeActor->DestroySceneCaptures(this);
	}

	if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
	{
		RestorePrimitiveVisibilityState();
	}
}

void UCompositeLayerShadowReflection::BeginDestroy()
{
	Super::BeginDestroy();

	OnRemoved(GetWorld()); // Redundant remove call for safety

	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
#if WITH_EDITOR
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void UCompositeLayerShadowReflection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Actors))
	{
		SetActors(MoveTemp(Actors));
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, AutoConfigureActors))
	{
		if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
		{
			const bool bHiddenInSceneCapture = (AutoConfigureActors == ECompositeHiddenInSceneCaptureConfiguration::Hidden);
			const bool bAffectIndirectLightingWhileHidden = bHiddenInSceneCapture;
			const bool bCastHiddenShadow = bHiddenInSceneCapture;

			UpdatePrimitiveVisibilityState(bHiddenInSceneCapture, bAffectIndirectLightingWhileHidden, bCastHiddenShadow);
		}
	}
}

void UCompositeLayerShadowReflection::PreEditUndo()
{
	Super::PreEditUndo();

	if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
	{
		RestorePrimitiveVisibilityState();
	}
}

void UCompositeLayerShadowReflection::PostEditUndo()
{
	Super::PostEditUndo();

	SetActors(MoveTemp(Actors));
}
#endif

bool UCompositeLayerShadowReflection::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return false;
	}

	CachedSceneCaptures = FindOrCreateSceneCapturePair(*CompositeActor);

	// Cached scene captures are now expected to be valid, early out if not.
	if (!ensure(CachedSceneCaptures[0].IsValid() && CachedSceneCaptures[1].IsValid()))
	{
		return false;
	}

	FMergePassProxy* DivisionProxy = nullptr;

	{
		FPassInputDeclArray Inputs;
		Inputs.SetNum(2);

		for (int32 Index = 0; Index < 2; ++Index)
		{
			UCompositeShadowReflectionCatcherComponent* Component = CachedSceneCaptures[Index].Get();
			FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(Component, Component->TextureTarget, GetRenderResolution());

			FResourceMetadata Metadata = {};
			Metadata.bInvertedAlpha = true;
			Metadata.bDistorted = Component->ShowFlags.LensDistortion;
			const ResourceId TexId = InContext.FindOrCreateExternalTexture(Component->TextureTarget, Metadata);

			Inputs[Index].Set<FPassExternalResourceDesc>({ TexId });
		}

		DivisionProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), ECompositeCoreMergeOp::Divide, TEXT("ShadowRefl Div"), ELensDistortionHandling::Disabled);
	}

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0].Set<const FCompositeCorePassProxy*>(DivisionProxy);
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("ShadowRefl Mul"));
	return true;
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerShadowReflection::GetActors() const
{
	return Actors;
}

void UCompositeLayerShadowReflection::SetActors(TArray<TSoftObjectPtr<AActor>> InActors)
{
	if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
	{
		RestorePrimitiveVisibilityState();
	}

	Actors = MoveTemp(InActors);

	if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
	{
		const bool bHiddenInSceneCapture = (AutoConfigureActors == ECompositeHiddenInSceneCaptureConfiguration::Hidden);
		const bool bAffectIndirectLightingWhileHidden = bHiddenInSceneCapture;
		const bool bCastHiddenShadow = bHiddenInSceneCapture;

		UpdatePrimitiveVisibilityState(bHiddenInSceneCapture, bAffectIndirectLightingWhileHidden, bCastHiddenShadow);
	}

	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return;
	}

	if (!CachedSceneCaptures[0].IsValid() || !CachedSceneCaptures[1].IsValid())
	{
		CachedSceneCaptures = FindOrCreateSceneCapturePair(*CompositeActor);
	}

	// Early exit if the cached scene captures are not valid
	if (!ensure(CachedSceneCaptures[0].IsValid() && CachedSceneCaptures[1].IsValid()))
	{
		return;
	}

	// First, we empty hidden actors since our previous logic used them
	CachedSceneCaptures[1]->HiddenActors.Empty();

	UpdateSceneCaptureComponents(*CachedSceneCaptures[1]);
}

void UCompositeLayerShadowReflection::UpdatePrimitiveVisibilityState(
	bool bInHiddenInSceneCapture,
	bool bInAffectIndirectLightingWhileHidden,
	bool bInCastHiddenShadow) const
{
	for (const TSoftObjectPtr<AActor>& Actor : Actors)
	{
		if (Actor.IsValid())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					bool bUpdatedComponent = false;

					if (PrimitiveComponent->bHiddenInSceneCapture != bInHiddenInSceneCapture)
					{
						PrimitiveComponent->SetHiddenInSceneCapture(bInHiddenInSceneCapture);
						bUpdatedComponent = true;
					}

					if (PrimitiveComponent->bAffectIndirectLightingWhileHidden != bInAffectIndirectLightingWhileHidden)
					{
						PrimitiveComponent->SetAffectIndirectLightingWhileHidden(bInAffectIndirectLightingWhileHidden);
						bUpdatedComponent = true;
					}

					if (PrimitiveComponent->bCastHiddenShadow != bInCastHiddenShadow)
					{
						PrimitiveComponent->SetCastHiddenShadow(bInCastHiddenShadow);
						bUpdatedComponent = true;
					}

					if (bUpdatedComponent)
					{
						PrimitiveComponent->Modify();
					}
				}
			}
		}
	}
}

void UCompositeLayerShadowReflection::RestorePrimitiveVisibilityState() const
{
	// Restore to class default
	UPrimitiveComponent* CDO = UPrimitiveComponent::StaticClass()->GetDefaultObject<UPrimitiveComponent>();

	UpdatePrimitiveVisibilityState(CDO->bHiddenInSceneCapture, CDO->bAffectIndirectLightingWhileHidden, CDO->bCastHiddenShadow);
}

void UCompositeLayerShadowReflection::UpdateSceneCaptureComponents(USceneCaptureComponent2D& SceneCaptureComponent) const
{
	SceneCaptureComponent.HiddenComponents.Reset(Actors.Num());

	for (const TSoftObjectPtr<AActor>& SoftActor : Actors)
	{
		if (const AActor* Actor = SoftActor.Get())
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component))
				{
					SceneCaptureComponent.HiddenComponents.Add(PrimitiveComponent);
				}
			}
		}
	}
}

void UCompositeLayerShadowReflection::OnEndOfFrameUpdate(UWorld* InWorld)
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
			if (CachedSceneCaptures[1].IsValid())
			{
				UpdateSceneCaptureComponents(*CachedSceneCaptures[1]);
			}
		}
	}
}

#if WITH_EDITOR
void UCompositeLayerShadowReflection::OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& EditPropertyChain)
{
	if (Object == this)
	{
		if (EditPropertyChain.GetActiveMemberNode())
		{
			const FProperty* Property = EditPropertyChain.GetActiveMemberNode()->GetValue();

			if (Property)
			{
				if (Property->GetName() == GET_MEMBER_NAME_CHECKED(UCompositeLayerShadowReflection, Actors))
				{
					if (AutoConfigureActors != ECompositeHiddenInSceneCaptureConfiguration::None)
					{
						RestorePrimitiveVisibilityState();
					}
				}
			}
		}
	}
}
#endif

TStaticArray<TWeakObjectPtr<UCompositeShadowReflectionCatcherComponent>, 2> UCompositeLayerShadowReflection::FindOrCreateSceneCapturePair(ACompositeActor& InOuter) const
{
	return {
		InOuter.FindOrCreateSceneCapture<UCompositeShadowReflectionCatcherComponent>(this, 0, FName("ShadowReflectionCatcher_CG")),
		InOuter.FindOrCreateSceneCapture<UCompositeShadowReflectionCatcherComponent>(this, 1, FName("ShadowReflectionCatcher_NoCG"))
	};
}

