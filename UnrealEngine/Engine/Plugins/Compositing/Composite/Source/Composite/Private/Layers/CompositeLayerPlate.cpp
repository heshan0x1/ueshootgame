// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerPlate.h"

#include "Components/StaticMeshComponent.h"
#include "CompositeActor.h"
#include "CompositeAssetUserData.h"
#include "CompositeCoreSubsystem.h"
#include "CompositeCoreSettings.h"
#include "CompositeRenderTargetPool.h"
#include "Engine/Texture2D.h"
#include "MediaTexture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Passes/CompositePassDistortion.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfile.h"
#include "Profile/MediaProfilePlaybackManager.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectSaveContext.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/TransactionObjectEvent.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

const FLazyName UCompositeLayerPlate::CompositeTextureName = FLazyName(TEXT("CompositeTexture"));

#define LOCTEXT_NAMESPACE "Composite"

namespace UE
{
	namespace Composite
	{
		namespace Private
		{
			/** Convenience function to remove composite asset user data from tracked mesh actors (before their removal). */
			static void RemoveCompositeAssetUserData(TConstArrayView<TSoftObjectPtr<AActor>> InPreEditMeshes, TConstArrayView<TSoftObjectPtr<AActor>> InPostEditMeshes)
			{
				for (const TSoftObjectPtr<AActor>& PreEditMesh : InPreEditMeshes)
				{
					if (!PreEditMesh.IsValid())
					{
						continue;
					}

					if (!InPostEditMeshes.Contains(PreEditMesh))
					{
						for (UActorComponent* Component : PreEditMesh->GetComponents())
						{
							UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);

							if (IsValid(PrimitiveComponent))
							{
								PrimitiveComponent->RemoveUserDataOfClass(UCompositeAssetUserData::StaticClass());
							}
						}
					}
				}
			}

			/** Convenience function to remove all composite asset user data from tracked mesh actors. */
			static void RemoveCompositeAllAssetUserData(TConstArrayView<TSoftObjectPtr<AActor>> InPreEditMeshes)
			{
				RemoveCompositeAssetUserData(InPreEditMeshes, {});
			}
		}
	}
}

UCompositeLayerPlate::UCompositeLayerPlate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> DefaultMediaTexture = TEXT("/Composite/Textures/T_Composite_SMPTE_Color_Bars_16x9.T_Composite_SMPTE_Color_Bars_16x9");
	
	Texture = Cast<UTexture>(DefaultMediaTexture.Object);
	PlateMode = ECompositePlateMode::CompositeMesh;
	Operation = ECompositeCoreMergeOp::Over;

	// Default undistort pass for plate media going into the scene
	ScenePasses.Add(CreateDefaultSubobject<UCompositePassDistortion>(TEXT("CompositeUndistortScenePass")));

#if WITH_EDITOR
	// If we are not the class default object...
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Hook into pre/post save.
		FEditorDelegates::PreSaveWorldWithContext.AddUObject(this, &UCompositeLayerPlate::OnPreSaveWorld);
		FEditorDelegates::PostSaveWorldWithContext.AddUObject(this, &UCompositeLayerPlate::OnPostSaveWorld);
	}
#endif // WITH_EDITOR
}

UCompositeLayerPlate::~UCompositeLayerPlate() = default;

void UCompositeLayerPlate::SetEnabled(bool bInEnabled)
{
	Super::SetEnabled(bInEnabled);
	
	PropagateStateChange(bInEnabled, GetWorld());
}

void UCompositeLayerPlate::Tick(float DeltaTime)
{
	if (!IsRendering())
	{
		return;
	}

	UCompositeCoreSubsystem* Subsystem = UWorld::GetSubsystem<UCompositeCoreSubsystem>(GetWorld());
	
	if (IsValid(Subsystem))
	{
		// TODO: We will need to separate the holdout feature from the custom render pass in the scene view extension, so that one can be used without the other.
		if (UE::CompositeCore::IsRegisterPrimitivesOnTickEnabled())
		{
			Subsystem->RegisterPrimitives(GetPrimitives());
		}
	}

	{
		// Constantly check if passes have changed to manage render targets & texture bindings on composite meshes.
		const int32 NumValidMediaPasses = GetValidPassesNum(MediaPasses);
		const int32 NumValidScenePasses = GetValidPassesNum(ScenePasses);
		bool bPassesHaveChanged = false;

		if (NumValidMediaPasses != CachedValidMediaPasses)
		{
			if (NumValidMediaPasses == 0)
			{
				FCompositeRenderTargetPool::Get().ReleaseTarget(MediaRenderTarget);
			}

			CachedValidMediaPasses = NumValidMediaPasses;
			bPassesHaveChanged = true;
		}

		if (NumValidScenePasses != CachedValidScenePasses)
		{
			if (NumValidScenePasses == 0)
			{
				FCompositeRenderTargetPool::Get().ReleaseTarget(SceneRenderTarget);
			}

			CachedValidScenePasses = NumValidScenePasses;
			bPassesHaveChanged = true;
		}

		if (bPassesHaveChanged)
		{
			UpdateCompositeMeshes();
		}
	}
}

ETickableTickType UCompositeLayerPlate::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UCompositeLayerPlate::IsTickable() const
{
	ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();

	// Only tick when instances are registered to a non-CDO composite actor.
	return IsValid(CompositeActor) && !CompositeActor->HasAnyFlags(RF_ClassDefaultObject);
}

UWorld* UCompositeLayerPlate::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

TStatId UCompositeLayerPlate::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCompositeLayerPlate, STATGROUP_Tickables);
}

void UCompositeLayerPlate::PostLoad()
{
	Super::PostLoad();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UpdateCompositeMeshes();
		TryOpenMediaProfileSource();
	}
}

void UCompositeLayerPlate::OnRemoved(const UWorld* World)
{
	FCompositeRenderTargetPool::Get().ReleaseTarget(MediaRenderTarget);
	FCompositeRenderTargetPool::Get().ReleaseTarget(SceneRenderTarget);

	TryCloseMediaProfileSource();

	constexpr bool bApply = false;
	PropagateStateChange(bApply, World);
}

void UCompositeLayerPlate::OnRenderingStateChange(bool bApply)
{
	PropagateStateChange(bApply, GetWorld());
}

void UCompositeLayerPlate::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	// Only remove delegates on final destruction to preserve them for undo operations.
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FEditorDelegates::PreSaveWorldWithContext.RemoveAll(this);
		FEditorDelegates::PostSaveWorldWithContext.RemoveAll(this);
	}
#endif // WITH_EDITOR

	OnRemoved(GetWorld()); // Redundant remove call for safety
}

const TArray<TSoftObjectPtr<AActor>> UCompositeLayerPlate::GetCompositeMeshes() const
{
	return CompositeMeshes;
}

void UCompositeLayerPlate::SetCompositeMeshes(TArray<TSoftObjectPtr<AActor>> InCompositeMeshes)
{
	// First, we untrack current meshes
	UE::Composite::Private::RemoveCompositeAssetUserData(CompositeMeshes, InCompositeMeshes);

	CompositeMeshes = MoveTemp(InCompositeMeshes);
	
	UpdateCompositeMeshes();
}

void UCompositeLayerPlate::SetPlateMode(ECompositePlateMode InPlateMode)
{
	PlateMode = InPlateMode;
}

UTexture* UCompositeLayerPlate::GetCompositeTexture() const
{
	if (CachedValidScenePasses > 0)
	{		
		return GetOrCreateRenderTarget(SceneRenderTarget).Get();
	}
	else if (CachedValidMediaPasses > 0)
	{
		return GetOrCreateRenderTarget(MediaRenderTarget).Get();
	}
	else if (IsValid(Texture))
	{
		return Texture.Get();
	}

	return nullptr;
}

#if WITH_EDITOR
bool UCompositeLayerPlate::CanEditChange(const FProperty* InProperty) const
{
	return Super::CanEditChange(InProperty);
}

void UCompositeLayerPlate::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);

	if (!PropertyThatWillChange)
	{
		return;
	}

	const FName PropertyName = PropertyThatWillChange->GetFName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, CompositeMeshes))
	{
		PreEditCompositeMeshes = CompositeMeshes;

		UCompositeCoreSubsystem* Subsystem = UWorld::GetSubsystem<UCompositeCoreSubsystem>(GetWorld());
		if (IsValid(Subsystem))
		{
			Subsystem->UnregisterPrimitives(GetPrimitives());
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Texture))
	{
		TryCloseMediaProfileSource();
	}
}

void UCompositeLayerPlate::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bIsEnabled))
	{
		SetEnabled(bIsEnabled);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, CompositeMeshes))
	{
		const bool bChangeWasRemoval = PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove
			|| PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear;

		if (bChangeWasRemoval)
		{
			UE::Composite::Private::RemoveCompositeAssetUserData(PreEditCompositeMeshes, CompositeMeshes);
		}
		else
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd ||
				PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet ||
				PropertyChangedEvent.ChangeType == EPropertyChangeType::ResetToDefault)
			{
				const int32 AlteredIndex = PropertyChangedEvent.GetArrayIndex(PropertyName.ToString());
				if (CompositeMeshes.IsValidIndex(AlteredIndex))
				{
					TSoftObjectPtr<AActor>& NewCompositeMesh = CompositeMeshes[AlteredIndex];

					if (IsCompositeMeshActorAlreadyInUse(NewCompositeMesh))
					{
						FNotificationInfo NotifyInfo(LOCTEXT("CompositeMeshAlreadyInUse", "The composite mesh is already in use by another layer."));
						NotifyInfo.ExpireDuration = 4.0f;
						FSlateNotificationManager::Get().AddNotification(NotifyInfo)->SetCompletionState(SNotificationItem::CS_Fail);

						NewCompositeMesh = nullptr;
					}
				}
			}
		}

		UCompositeCoreSubsystem* Subsystem = UWorld::GetSubsystem<UCompositeCoreSubsystem>(GetWorld());
		if (IsValid(Subsystem) && IsRendering())
		{
			// Register primitives to be marked as holdout and rendered in the built-in composite custom render pass.
			Subsystem->RegisterPrimitives(GetPrimitives());
		}

		PreEditCompositeMeshes.Reset();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Texture))
	{
		TryOpenMediaProfileSource();
	}

	// For simplicity, we make sure to update composite meshes on any property change.
	UpdateCompositeMeshes();
}

void UCompositeLayerPlate::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);

	if (InTransactionEvent.HasPropertyChanges())
	{
		const TArray<FName>& ChangedProperties = InTransactionEvent.GetChangedProperties();

		if( ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(ThisClass, Texture)))
		{
			if (InTransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
			{
				// Special processing for undo/redo as PreEdit and PostEdit are not called during undo/redo
				// Attempt to close any outstanding media profile sources this plate may have opened for Texture,
				// and then attempt to open the corresponding media source if Texture is a media profile media texture
				TryCloseMediaProfileSource();
				TryOpenMediaProfileSource();
			}
			
			UpdateCompositeMeshes();
		}
		else if (ChangedProperties.Contains(GET_MEMBER_NAME_CHECKED(ThisClass, CompositeMeshes)))
		{
			UpdateCompositeMeshes();

			UCompositeCoreSubsystem* Subsystem = UWorld::GetSubsystem<UCompositeCoreSubsystem>(GetWorld());
			if (IsValid(Subsystem) && IsRendering())
			{
				// Register primitives to be marked as holdout and rendered in the built-in composite custom render pass.
				Subsystem->RegisterPrimitives(GetPrimitives());
			}
		}
	}
}
#endif


int32 UCompositeLayerPlate::FindLastValidPassIndex(TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const
{
	int32 LastValidPassIndex = INDEX_NONE;
	
	// Iterate backwards so that lower passes in the UI are executed first
	for (int32 PassIndex = InPasses.Num() - 1; PassIndex >= 0; --PassIndex)
	{
		const TObjectPtr<UCompositePassBase>& Pass = InPasses[PassIndex];

		if (IsValid(Pass) && Pass->IsActive())
		{
			LastValidPassIndex = PassIndex;
		}
	}

	return LastValidPassIndex;
}

UE::CompositeCore::ResourceId UCompositeLayerPlate::AddPreprocessingPasses(
	FTraversalContext& InContext,
	FSceneRenderingBulkObjectAllocator& InFrameAllocator,
	TArrayView<const TObjectPtr<UCompositePassBase>> InPasses,
	UE::CompositeCore::ResourceId TextureId,
	UE::CompositeCore::ResourceId OriginalTextureId,
	TFunction<TObjectPtr<UTextureRenderTarget2D>()> GetRenderTargetFn
) const
{
	using namespace UE::CompositeCore;

	if (InPasses.IsEmpty())
	{
		return TextureId;
	}

	ResourceId OutputTextureId = TextureId;
	bool bIsFirstValidPass = true;
	const int32 LastValidPassIndex = FindLastValidPassIndex(InPasses);

	// Iterate backwards so that lower passes in the UI are executed first
	for (int32 PassIndex = InPasses.Num() - 1; PassIndex >= 0; --PassIndex)
	{
		const TObjectPtr<UCompositePassBase>& Prepass = InPasses[PassIndex];

		if (!IsValid(Prepass) || !Prepass->IsActive())
		{
			continue;
		}

		FPassInputDecl PassInput;

		if (bIsFirstValidPass)
		{
			PassInput.Set<FPassExternalResourceDesc>({ TextureId });
			bIsFirstValidPass = false;
		}
		else
		{
			// Default input from previous pass
			PassInput.Set<FPassInternalResourceDesc>({});
		}

		FCompositeCorePassProxy* PrepassProxy;
		if (Prepass->GetProxy(PassInput, InFrameAllocator, PrepassProxy))
		{
			if (PassIndex == LastValidPassIndex)
			{
				FResourceMetadata Metadata = {};
				Metadata.bDistorted = true;
				OutputTextureId = InContext.FindOrCreateExternalTexture(GetRenderTargetFn(), Metadata);

				PrepassProxy->DeclareOutputOverride(OutputTextureId);
			}

			InContext.PreprocessingPasses.FindOrAdd(OriginalTextureId).Add(PrepassProxy);
		}
	}

	return OutputTextureId;
}

bool UCompositeLayerPlate::GetProxy(FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	using namespace UE::CompositeCore;

	ResourceId TextureId = BUILT_IN_EMPTY_ID;

	if (IsValid(Texture))
	{
		// Register source texture into the frame work context (so that post-processing passes can refer to it).
		FResourceMetadata Metadata = {};
		Metadata.bDistorted = true;
		TextureId = InContext.FindOrCreateExternalTexture(Texture, Metadata);
		
		const ResourceId OriginalTextureId = TextureId;

		// Add media texture pre-processing passes
		TextureId = AddPreprocessingPasses(InContext, InFrameAllocator, MediaPasses, TextureId, OriginalTextureId,
			[this]() -> TObjectPtr<UTextureRenderTarget2D>
			{
				return GetOrCreateRenderTarget(MediaRenderTarget);
			}
		);

		// Add scene-only passes. We ignore returned resource since we don't refer to it in post-processing.
		AddPreprocessingPasses(InContext, InFrameAllocator, ScenePasses, TextureId, OriginalTextureId,
			[this]() -> TObjectPtr<UTextureRenderTarget2D>
			{
				return GetOrCreateRenderTarget(SceneRenderTarget);
			}
		);
	}

	if (PlateMode == ECompositePlateMode::CompositeMesh && GetValidPrimitivesNum() > 0)
	{
		// Prioritize the built-in custom render pass when enabled
		TextureId = BUILT_IN_CRP_ID;
	}

	FPassInputDecl PassInput;
	PassInput.Set<FPassExternalResourceDesc>({ TextureId });

	AddChildPasses(PassInput, InContext, InFrameAllocator, LayerPasses);

	FPassInputDeclArray Inputs;
	Inputs.SetNum(FixedNumLayerInputs);
	Inputs[0] = PassInput;
	Inputs[1] = GetDefaultSecondInput(InContext);

	OutProxy = InFrameAllocator.Create<FMergePassProxy>(MoveTemp(Inputs), GetMergeOperation(InContext), TEXT("Plate"));
	return true;
}

TArray<UPrimitiveComponent*> UCompositeLayerPlate::GetPrimitives() const
{
	const UCompositeCorePluginSettings* Settings = GetDefault<UCompositeCorePluginSettings>();
	check(Settings);

	TArray<UPrimitiveComponent*> OutPrimitiveComponents;

	for (const TSoftObjectPtr<AActor>& Actor : CompositeMeshes)
	{
		if (!Actor.IsValid())
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimComponents(Actor.Get());

		for (UPrimitiveComponent* PrimitiveComponent : PrimComponents)
		{
			if (Settings->IsAllowedPrimitiveClass(PrimitiveComponent))
			{
				OutPrimitiveComponents.Add(PrimitiveComponent);
			}
		}
	}

	return OutPrimitiveComponents;
}

int32 UCompositeLayerPlate::GetValidPrimitivesNum() const
{
	const UCompositeCorePluginSettings* Settings = GetDefault<UCompositeCorePluginSettings>();
	check(Settings);

	int32 Count = 0;

	for (const TSoftObjectPtr<AActor>& Actor : CompositeMeshes)
	{
		if (!Actor.IsValid())
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimComponents(Actor.Get());

		for (UPrimitiveComponent* PrimitiveComponent : PrimComponents)
		{
			if (Settings->IsAllowedPrimitiveClass(PrimitiveComponent))
			{
				++Count;
			}
		}
	}

	return Count;
}

int32 UCompositeLayerPlate::GetValidPassesNum(TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const
{
	int32 Count = 0;

	for (int32 Index = 0; Index < InPasses.Num(); ++Index)
	{
		const TObjectPtr<UCompositePassBase>& Pass = InPasses[Index];

		if (IsValid(Pass) && Pass->IsActive())
		{
			++Count;
		}
	}

	return Count;
}

void UCompositeLayerPlate::TryOpenMediaProfileSource()
{
	UMediaTexture* MediaTexture = Cast<UMediaTexture>(Texture);
	if (!MediaTexture)
	{
		return;
	}
	
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	if (!ActiveMediaProfile)
	{
		return;
	}
	
	int32 MediaSourceIndex = INDEX_NONE;
	if (ActiveMediaProfile->GetPlaybackManager()->IsValidSourceMediaTexture(MediaTexture, MediaSourceIndex))
	{
		ActiveMediaProfile->GetPlaybackManager()->OpenSourceFromIndex(MediaSourceIndex, this);
	}
}

void UCompositeLayerPlate::TryCloseMediaProfileSource()
{
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	if (!ActiveMediaProfile)
	{
		return;
	}

	UMediaProfilePlaybackManager::FCloseSourceArgs Args;
	Args.Consumer = this;
				
	ActiveMediaProfile->GetPlaybackManager()->CloseSourcesForConsumer(Args);
}

bool UCompositeLayerPlate::IsRendering() const
{
	if (IsEnabled())
	{
		const ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();

		return IsValid(CompositeActor) && CompositeActor->IsRendering();
	}

	return false;
}

#if WITH_EDITOR
void UCompositeLayerPlate::OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext ObjectSaveContext)
{
	// We need to remove our asset user data before saving, as we do not need to save it out
	// and only use it to know when the static mesh component changes.
	UE::Composite::Private::RemoveCompositeAllAssetUserData(CompositeMeshes);
}

void UCompositeLayerPlate::OnPostSaveWorld(UWorld* InWorld, FObjectPostSaveContext ObjectSaveContext)
{
	constexpr bool bRegisterOnly = true;
	UpdateCompositeMeshes(bRegisterOnly);
}
#endif

void UCompositeLayerPlate::UpdateCompositeMeshes(bool bRegisterOnly) const
{
	for (const TSoftObjectPtr<AActor>& MeshActor : CompositeMeshes)
	{
		if (!MeshActor.IsValid())
		{
			continue;
		}

		for (UActorComponent* Component : MeshActor->GetComponents())
		{
			UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);

			if (!IsValid(PrimitiveComponent))
			{
				continue;
			}

			// Track meshes so that we can update their material upon changes
			if (!PrimitiveComponent->HasAssetUserDataOfClass(UCompositeAssetUserData::StaticClass()))
			{
				UCompositeAssetUserData* AssetUserData = NewObject<UCompositeAssetUserData>(PrimitiveComponent);
				AssetUserData->OnPostEditChangeOwner.BindUObject(this, &UCompositeLayerPlate::UpdatePrimitiveComponent);
				PrimitiveComponent->AddAssetUserData(AssetUserData);
			}

			if (!bRegisterOnly)
			{
				UpdatePrimitiveComponent(*PrimitiveComponent);
			}
		}
	}
}

void UCompositeLayerPlate::UpdatePrimitiveComponent(UPrimitiveComponent& InPrimitiveComponent) const
{
	for (int32 ElementIndex = 0; ElementIndex < InPrimitiveComponent.GetNumMaterials(); ++ElementIndex)
	{
		UMaterialInterface* MaterialInterface = InPrimitiveComponent.GetMaterial(ElementIndex);

		if (!IsValid(MaterialInterface))
		{
			continue;
		}

		TArray<FMaterialParameterInfo> ParameterInfo;
		TArray<FGuid> ParameterIds;
		MaterialInterface->GetAllTextureParameterInfo(ParameterInfo, ParameterIds);

		// Avoid conversion to MID if parameter doesn't exist
		const bool bHasTextureParameter = ParameterInfo.ContainsByPredicate([](const FMaterialParameterInfo& ParamInfo)
			{
				return ParamInfo.Name.IsEqual(CompositeTextureName);
			}
		);

		if (bHasTextureParameter)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MaterialInterface);

			if (!MID)
			{
				/**
				* Note: Unfortunately, MIDs are the only currently viable approach to bind textures to composite meshes.
				* Texture collection could provide a nice alternative, but are experimental, have limited support and do
				* not provide the same world-unique instance functionality that MPCs do, which would cause dynamic
				* bindings to dirty a texture collection asset.
				*/
				MID = InPrimitiveComponent.CreateAndSetMaterialInstanceDynamic(ElementIndex);
			}
			
			MID->SetScalarParameterValue(TEXT("UseCompositeProjection"), static_cast<float>(PlateMode == ECompositePlateMode::CompositeMesh));
			MID->SetTextureParameterValue(TEXT("CompositeTexture"), GetCompositeTexture());
		}
	}
}

void UCompositeLayerPlate::PropagateStateChange(bool bApply, const UWorld* SourceWorld) const
{
	if (CompositeMeshes.IsEmpty())
	{
		return;
	}

	UCompositeCoreSubsystem* Subsystem = UWorld::GetSubsystem<UCompositeCoreSubsystem>(SourceWorld ? SourceWorld : GetWorld());

	if (bApply && IsRendering())
	{
		constexpr bool bRegisterOnly = true;
		UpdateCompositeMeshes(bRegisterOnly);

		if (IsValid(Subsystem))
		{
			// Rely on the scene view extension to mark primivites as holdout, and render them separately.
			Subsystem->RegisterPrimitives(GetPrimitives());
		}
	}
	else
	{
		UE::Composite::Private::RemoveCompositeAllAssetUserData(CompositeMeshes);

		if (IsValid(Subsystem))
		{
			Subsystem->UnregisterPrimitives(GetPrimitives());
		}
	}
}

bool UCompositeLayerPlate::IsCompositeMeshActorAlreadyInUse(TSoftObjectPtr<AActor> InCompositeMeshActor) const
{
	const ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	if (!IsValid(CompositeActor))
	{
		return false;
	}

	for (const TObjectPtr<UCompositeLayerBase>& Layer : CompositeActor->GetCompositeLayers())
	{
		const UCompositeLayerPlate* PlateLayer = Cast<UCompositeLayerPlate>(Layer.Get());
		if (IsValid(PlateLayer) && PlateLayer != this)
		{
			if (PlateLayer->CompositeMeshes.Contains(InCompositeMeshActor))
			{
				return true;
			}
		}
	}

	return false;
}

TObjectPtr<UTextureRenderTarget2D> UCompositeLayerPlate::GetOrCreateRenderTarget(TObjectPtr<UTextureRenderTarget2D>& InRenderTarget) const
{
	if (!IsValid(InRenderTarget))
	{
		FCompositeRenderTargetPool::Get().ConditionalAcquireTarget(this, InRenderTarget, GetRenderResolution());
	}

	return InRenderTarget;
}

#undef LOCTEXT_NAMESPACE
