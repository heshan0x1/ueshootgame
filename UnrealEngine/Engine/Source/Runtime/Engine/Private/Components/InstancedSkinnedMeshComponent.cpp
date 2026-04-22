// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/InstancedSkinnedMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h" // For SMInstanceElementDataUtil::SMInstanceElementsEnabled
#include "Elements/SMInstance/SMInstanceElementId.h"
#include "Engine/StaticMesh.h"
#include "HitProxies.h"
#include "NaniteSceneProxy.h"
#include "PrimitiveSceneInfo.h"
#include "Rendering/NaniteResourcesHelper.h"
#include "Rendering/RenderCommandPipes.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneInterface.h"
#include "SkeletalRenderNanite.h"
#include "SkeletalRenderGPUSkin.h"
#include "SkinningDefinitions.h"
#include "SkinnedMeshSceneProxyDesc.h"
#include "InstanceData/InstanceUpdateChangeSet.h"
#include "InstanceData/InstanceDataUpdateUtils.h"
#include "InstancedSkinnedMeshComponentHelper.h"
#include "PrimitiveSceneDesc.h"
#include "InstancedSkinnedMeshSceneProxy.h"

#if WITH_EDITOR
#include "WorldPartition/HLOD/HLODHashBuilder.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InstancedSkinnedMeshComponent)

TAutoConsoleVariable<int32> CVarInstancedSkinnedMeshesForceRefPose(
	TEXT("r.InstancedSkinnedMeshes.ForceRefPose"),
	0,
	TEXT("Whether to force ref pose for instanced skinned meshes"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarInstancedSkinnedMeshesAnimationBounds(
	TEXT("r.InstancedSkinnedMeshes.AnimationBounds"),
	1,
	TEXT("Whether to use animation bounds for instanced skinned meshes"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_RenderThreadSafe
);

static FSkeletalMeshObject* CreateInstancedSkeletalMeshObjectFunction(void* UserData, USkinnedMeshComponent* InComponent, FSkeletalMeshRenderData* InRenderData, ERHIFeatureLevel::Type InFeatureLevel)
{
	return FInstancedSkinnedMeshSceneProxyDesc::CreateMeshObject(FInstancedSkinnedMeshSceneProxyDesc(Cast<UInstancedSkinnedMeshComponent>(InComponent)), InRenderData, InFeatureLevel);
}

UInstancedSkinnedMeshComponent::UInstancedSkinnedMeshComponent(FVTableHelper& Helper)
: Super(Helper)
, bInheritPerInstanceData(false)
, InstanceDataManager(this)
{
}

UInstancedSkinnedMeshComponent::UInstancedSkinnedMeshComponent(const FObjectInitializer& ObjectInitializer) 
: Super(ObjectInitializer) 
, bInheritPerInstanceData(false)
, InstanceDataManager(this)
{
}

UInstancedSkinnedMeshComponent::~UInstancedSkinnedMeshComponent()
{
}

bool UInstancedSkinnedMeshComponent::ShouldForceRefPose()
{ 
	return CVarInstancedSkinnedMeshesForceRefPose.GetValueOnAnyThread() != 0;
}

bool UInstancedSkinnedMeshComponent::ShouldUseAnimationBounds()
{
	return CVarInstancedSkinnedMeshesAnimationBounds.GetValueOnAnyThread() != 0;
}

struct FSkinnedMeshInstanceData_Deprecated
{
	FMatrix Transform;
	uint32 AnimationIndex;
	uint32 Padding[3]; // Need to respect 16 byte alignment for bulk-serialization

	FSkinnedMeshInstanceData_Deprecated()
	: Transform(FMatrix::Identity)
	, AnimationIndex(0)
	{
		Padding[0] = 0;
		Padding[1] = 0;
		Padding[2] = 0;
	}

	FSkinnedMeshInstanceData_Deprecated(const FMatrix& InTransform, uint32 InAnimationIndex)
	: Transform(InTransform)
	, AnimationIndex(InAnimationIndex)
	{
		Padding[0] = 0;
		Padding[1] = 0;
		Padding[2] = 0;
	}

	friend FArchive& operator<<(FArchive& Ar, FSkinnedMeshInstanceData_Deprecated& InstanceData)
	{
		// @warning BulkSerialize: FSkinnedMeshInstanceData is serialized as memory dump
		// See TArray::BulkSerialize for detailed description of implied limitations.
		Ar << InstanceData.Transform;
		Ar << InstanceData.AnimationIndex;
		Ar << InstanceData.Padding[0];
		Ar << InstanceData.Padding[1];
		Ar << InstanceData.Padding[2];
		return Ar;
	}
};

void UInstancedSkinnedMeshComponent::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	// Inherit properties when bEditableWhenInherited == false || bInheritPerInstanceData == true (when the component isn't a template and we are persisting data)
	const UInstancedSkinnedMeshComponent* Archetype = Cast<UInstancedSkinnedMeshComponent>(GetArchetype());
	const bool bInheritSkipSerializationProperties = ShouldInheritPerInstanceData(Archetype) && Ar.IsPersistent();
	
	// Check if we need have SkipSerialization property data to load/save
	bool bHasSkipSerializationPropertiesData = !bInheritSkipSerializationProperties;
	Ar << bHasSkipSerializationPropertiesData;

	if (Ar.IsLoading())
	{
		// Read existing data if it was serialized
		TArray<FSkinnedMeshInstanceData> TempInstanceData;
		TArray<float> TempInstanceCustomData;

		if (bHasSkipSerializationPropertiesData)
		{
			if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::SkinnedMeshInstanceDataSerializationV2)
			{
				TArray<FSkinnedMeshInstanceData_Deprecated> TempInstanceData_Deprecated;
				TempInstanceData_Deprecated.BulkSerialize(Ar, false /* force per element serialization */);

				TempInstanceData.Reserve(TempInstanceData_Deprecated.Num());
				for (const auto& Item : TempInstanceData_Deprecated)
				{
					TempInstanceData.Emplace(FTransform3f(FMatrix44f(Item.Transform)), Item.AnimationIndex);
				}
			}
			else
			{
				Ar << TempInstanceData;
			}
			TempInstanceCustomData.BulkSerialize(Ar);
		}

		// If we should inherit use Archetype Data
		if (bInheritSkipSerializationProperties)
		{
			ApplyInheritedPerInstanceData(Archetype);
		} 
		// It is possible for a component to lose its BP archetype between a save / load so in this case we have no per instance data (usually this component gets deleted through construction script)
		else if (bHasSkipSerializationPropertiesData)
		{
			InstanceData = MoveTemp(TempInstanceData);
			InstanceCustomData = MoveTemp(TempInstanceCustomData);
		}
	}
	else if (bHasSkipSerializationPropertiesData)
	{
		Ar << InstanceData;
		InstanceCustomData.BulkSerialize(Ar);
	}

#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		Ar << SelectedInstances;
	}
#endif

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::SkinnedMeshInstanceDataSerializationV2)
	{
		InstanceDataManager.Serialize(Ar, bCooked);
	}
	else if (Ar.IsLoading())
	{
		// Prior to this the id mapping was not saved so we need to reset it.
		InstanceDataManager.Reset(InstanceData.Num());
	}

	if (bCooked)
	{
		if (Ar.IsLoading())
		{
			InstanceDataManager.ReadCookedRenderData(Ar);
		}
#if WITH_EDITOR
		else if (Ar.IsSaving())
		{
			InstanceDataManager.WriteCookedRenderData(Ar, GetComponentDesc(GMaxRHIShaderPlatform));
		}
#endif
	}
}

void UInstancedSkinnedMeshComponent::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (GIsEditor)
	{
		SetSkinnedAssetCallback();
	}
#endif
}

void UInstancedSkinnedMeshComponent::BeginDestroy()
{
	Super::BeginDestroy();
}

void UInstancedSkinnedMeshComponent::OnRegister()
{
	Super::OnRegister();
}

void UInstancedSkinnedMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool UInstancedSkinnedMeshComponent::IsEnabled() const
{
	return FInstancedSkinnedMeshComponentHelper::IsEnabled(*this);
}

int32 UInstancedSkinnedMeshComponent::GetInstanceCount() const
{
	return bIsInstanceDataGPUOnly ? NumInstancesGPUOnly : InstanceData.Num();
}

UTransformProviderData* UInstancedSkinnedMeshComponent::GetTransformProvider() const
{
	return TransformProvider.Get();
}

void UInstancedSkinnedMeshComponent::SetTransformProvider(UTransformProviderData* InTransformProvider)
{
	TransformProvider = InTransformProvider;
	// We use the transform dirty state to drive the update of the animation data (to defer the need to add more bits), so we mark those as dirty here.
	InstanceDataManager.TransformsChangedAll();
	MarkRenderStateDirty();
}

template <typename ArrayType>
inline void ReorderArray(ArrayType& InOutDataArray, const TArray<int32>& OldIndexArray, int32 ElementStride = 1)
{
	check(OldIndexArray.Num() * ElementStride == InOutDataArray.Num());
	ArrayType TmpDataArray = MoveTemp(InOutDataArray);
	InOutDataArray.Empty(TmpDataArray.Num());
	for (int32 NewIndex = 0; NewIndex < OldIndexArray.Num(); ++NewIndex)
	{
		int32 OldIndex = OldIndexArray[NewIndex];
		for (int32 SubIndex = 0; SubIndex < ElementStride; ++SubIndex)
		{
			InOutDataArray.Add(TmpDataArray[OldIndex * ElementStride + SubIndex]);
		}
	}
}

void UInstancedSkinnedMeshComponent::OptimizeInstanceData(bool bShouldRetainIdMap)
{
	// compute the optimal order 
	TArray<int32> IndexRemap = InstanceDataManager.Optimize(GetComponentDesc(GMaxRHIShaderPlatform), bShouldRetainIdMap);
	
	if (!IndexRemap.IsEmpty())
	{
		// Reorder instances according to the remap
		ReorderArray(InstanceData, IndexRemap);
		ReorderArray(InstanceCustomData, IndexRemap, NumCustomDataFloats);
#if WITH_EDITOR
		ReorderArray(SelectedInstances, IndexRemap);
#endif
	}
}

void UInstancedSkinnedMeshComponent::ApplyInheritedPerInstanceData(const UInstancedSkinnedMeshComponent* InArchetype)
{
	check(InArchetype);
	InstanceData = InArchetype->InstanceData;
	InstanceCustomData = InArchetype->InstanceCustomData;
	NumCustomDataFloats = InArchetype->NumCustomDataFloats;
}

bool UInstancedSkinnedMeshComponent::ShouldInheritPerInstanceData() const
{
	return ShouldInheritPerInstanceData(Cast<UInstancedSkinnedMeshComponent>(GetArchetype()));
}

bool UInstancedSkinnedMeshComponent::ShouldInheritPerInstanceData(const UInstancedSkinnedMeshComponent* InArchetype) const
{
	return (bInheritPerInstanceData || !bEditableWhenInherited) && InArchetype && InArchetype->IsInBlueprint() && !IsTemplate();
}

void UInstancedSkinnedMeshComponent::SetInstanceDataGPUOnly(bool bInInstancesGPUOnly)
{
	if (bIsInstanceDataGPUOnly != bInInstancesGPUOnly)
	{
		bIsInstanceDataGPUOnly = bInInstancesGPUOnly;

		if (bIsInstanceDataGPUOnly)
		{
			ClearInstances();
		}
	}
}

void UInstancedSkinnedMeshComponent::SetupNewInstanceData(FSkinnedMeshInstanceData& InOutNewInstanceData, int32 InInstanceIndex, const FTransform3f& InInstanceTransform, int32 InAnimationIndex)
{
	InOutNewInstanceData.Transform = InInstanceTransform;
	InOutNewInstanceData.AnimationIndex = InAnimationIndex;

	if (bPhysicsStateCreated)
	{
		// ..
	}
}

const Nanite::FResources* UInstancedSkinnedMeshComponent::GetNaniteResources() const
{
	return Super::GetNaniteResources();
}

#if WITH_EDITOR

void UInstancedSkinnedMeshComponent::PostAssetCompilation()
{
	InstanceDataManager.ClearChangeTracking();
	MarkRenderStateDirty();
}

#endif 

FInstanceDataManagerSourceDataDesc UInstancedSkinnedMeshComponent::GetComponentDesc(EShaderPlatform InShaderPlatform)
{
	return FInstancedSkinnedMeshComponentHelper::GetComponentDesc(*this, InShaderPlatform);
}

void UInstancedSkinnedMeshComponent::SendRenderInstanceData_Concurrent()
{
	Super::SendRenderInstanceData_Concurrent();

	// If instance data is entirely GPU driven, don't upload from CPU.
	if (bIsInstanceDataGPUOnly)
	{
		return;
	}

	// If the primitive isn't hidden update its instances.
	const bool bDetailModeAllowsRendering = true;//DetailMode <= GetCachedScalabilityCVars().DetailMode;
	// The proxy may not be created, this can happen when a SM is async loading for example.
	if (bDetailModeAllowsRendering && (ShouldRender() || bCastHiddenShadow || bAffectIndirectLightingWhileHidden || bRayTracingFarField))
	{
		if (SceneProxy != nullptr)
		{
			// Make sure the instance data proxy is up to date:
			if (InstanceDataManager.FlushChanges(GetComponentDesc(SceneProxy->GetScene().GetShaderPlatform())))
			{
				UpdateBounds();
				GetWorld()->Scene->UpdatePrimitiveInstances(this);
			}
		}
		else
		{
			UpdateBounds();
			GetWorld()->Scene->AddPrimitive(this);
		}
	}
}

bool UInstancedSkinnedMeshComponent::IsHLODRelevant() const
{
	if (!CanBeHLODRelevant(this))
	{
		return false;
	}

	if (!GetSkinnedAsset())
	{
		return false;
	}

	if (!IsVisible())
	{
		return false;
	}

	if (Mobility == EComponentMobility::Movable)
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (!bEnableAutoLODGeneration)
	{
		return false;
	}
#endif

	return true;
}

#if WITH_EDITOR
void UInstancedSkinnedMeshComponent::ComputeHLODHash(FHLODHashBuilder& HashBuilder) const
{
	Super::ComputeHLODHash(HashBuilder);

	HashBuilder.HashField(InstanceData, GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceData));
	HashBuilder.HashField(GetTransformProvider(), TEXT("TransformProvider"));
	HashBuilder.HashField(InstanceCustomData, GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceCustomData));
	HashBuilder.HashField(InstanceMinDrawDistance, GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceMinDrawDistance));
	HashBuilder.HashField(InstanceStartCullDistance, GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceStartCullDistance));
	HashBuilder.HashField(InstanceEndCullDistance, GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceEndCullDistance));

	HashBuilder << GetSkinnedAsset();
}
#endif

void UInstancedSkinnedMeshComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	MeshObjectFactory = &CreateInstancedSkeletalMeshObjectFunction;
	Super::CreateRenderState_Concurrent(Context);
}

void UInstancedSkinnedMeshComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
}

FPrimitiveSceneProxy* UInstancedSkinnedMeshComponent::CreateSceneProxy()
{
	LLM_SCOPE(ELLMTag::SkeletalMesh);

	ERHIFeatureLevel::Type SceneFeatureLevel = GetWorld()->GetFeatureLevel();
	FPrimitiveSceneProxy* Result = nullptr;
	FSkeletalMeshRenderData* SkelMeshRenderData = GetSkeletalMeshRenderData();

#if WITH_EDITOR
	if (!bIsInstanceDataApplyCompleted)
	{
		return nullptr;
	}
#endif

	const USkinnedAsset* SkinnedAssetPtr = GetSkinnedAsset();
	if (GetInstanceCount() == 0 || SkinnedAssetPtr == nullptr || SkinnedAssetPtr->IsCompiling())
	{
		return nullptr;
	}

	if (TransformProvider != nullptr && TransformProvider->IsEnabled())
	{
		if (TransformProvider->IsCompiling())
		{
			return nullptr;
		}
	}

	if (CheckPSOPrecachingAndBoostPriority() && GetPSOPrecacheProxyCreationStrategy() == EPSOPrecacheProxyCreationStrategy::DelayUntilPSOPrecached)
	{
		UE_LOG(LogAnimBank, Verbose, TEXT("Skipping CreateSceneProxy for UInstancedSkinnedMeshComponent %s (UInstancedSkinnedMeshComponent PSOs are still compiling)"), *GetFullName());
		return nullptr;
	}

	GetOrCreateInstanceDataSceneProxy();

	Result = FInstancedSkinnedMeshSceneProxyDesc::CreateSceneProxy(FInstancedSkinnedMeshSceneProxyDesc(this), bHideSkin, ShouldNaniteSkin(), IsEnabled(), ComputeMinLOD());

	// Unclear exactly how this is supposed to work with a non-instanced proxy - will be interesting...
	// If GPU-only flag set, instance data is entirely GPU driven, don't upload from CPU.
	if (Result && !bIsInstanceDataGPUOnly)
	{
		InstanceDataManager.FlushChanges(GetComponentDesc(Result->GetScene().GetShaderPlatform()));
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	SendRenderDebugPhysics(Result);
#endif

	return Result;
}

void UInstancedSkinnedMeshComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
	InstanceDataManager.PrimitiveTransformChanged();
}

#if WITH_EDITOR

void UInstancedSkinnedMeshComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Always clear the change tracking because in the editor, attributes may have been set without any sort of notification
	InstanceDataManager.ClearChangeTracking();
	if (PropertyChangedEvent.Property != nullptr)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, InstanceData))
		{
			if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd
				|| PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
			{
				int32 AddedAtIndex = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.Property->GetFName().ToString());
				check(AddedAtIndex != INDEX_NONE);

				AddInstanceInternal(
					AddedAtIndex,
					PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd ? FTransform::Identity : FTransform(InstanceData[AddedAtIndex].Transform),
					PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd ? 0 : InstanceData[AddedAtIndex].AnimationIndex,
					/*bWorldSpace*/false
				);
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove)
			{
				int32 RemovedAtIndex = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.Property->GetFName().ToString());
				check(RemovedAtIndex != INDEX_NONE);

				RemoveInstanceInternal(RemovedAtIndex, true);
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear)
			{
				ClearInstances();
			}
			else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
			{
			}
			MarkRenderStateDirty();
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSkinnedMeshInstanceData, Transform)
			 || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSkinnedMeshInstanceData, AnimationIndex))
		{
			MarkRenderStateDirty();
		}
		else if (PropertyChangedEvent.Property->GetFName() == "NumCustomDataFloats")
		{
			SetNumCustomDataFloats(NumCustomDataFloats);
		}
		else if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName() == "InstanceCustomData")
		{
			int32 ChangedCustomValueIndex = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.Property->GetFName().ToString());
			if (ensure(NumCustomDataFloats > 0))
			{
				int32 InstanceIndex = ChangedCustomValueIndex / NumCustomDataFloats;
			}
			MarkRenderStateDirty();
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UInstancedSkinnedMeshComponent, TransformProvider))
		{
			MarkRenderStateDirty();
		}
	}
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UInstancedSkinnedMeshComponent::PostEditUndo()
{
	Super::PostEditUndo();
	MarkRenderStateDirty();
}

void UInstancedSkinnedMeshComponent::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	if (TransformProvider != nullptr && TransformProvider->IsEnabled())
	{
		TransformProvider->BeginCacheForCookedPlatformData(TargetPlatform);
	}
}

bool UInstancedSkinnedMeshComponent::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	if (TransformProvider != nullptr)
	{
		if (!TransformProvider->IsCachedCookedPlatformDataLoaded(TargetPlatform))
		{
			return false;
		}
	}

	return Super::IsCachedCookedPlatformDataLoaded(TargetPlatform);
}

#endif

TStructOnScope<FActorComponentInstanceData> UInstancedSkinnedMeshComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> ComponentInstanceData;
#if WITH_EDITOR
	ComponentInstanceData.InitializeAs<FInstancedSkinnedMeshComponentInstanceData>(this);
	FInstancedSkinnedMeshComponentInstanceData* SkinnedMeshInstanceData = ComponentInstanceData.Cast<FInstancedSkinnedMeshComponentInstanceData>();

	// Back up per-instance info (this is strictly for Comparison in UInstancedSkinnedMeshComponent::ApplyComponentInstanceData 
	// as this Property will get serialized by base class FActorComponentInstanceData through FComponentPropertyWriter which uses the PPF_ForceTaggedSerialization to backup all properties even the custom serialized ones
	SkinnedMeshInstanceData->InstanceData = InstanceData;

	// Back up instance selection
	SkinnedMeshInstanceData->SelectedInstances = SelectedInstances;

	// Back up per-instance hit proxies
	SkinnedMeshInstanceData->bHasPerInstanceHitProxies = bHasPerInstanceHitProxies;
#endif
	return ComponentInstanceData;
}

void UInstancedSkinnedMeshComponent::SetCullDistances(int32 StartCullDistance, int32 EndCullDistance)
{
	if (InstanceStartCullDistance != StartCullDistance || InstanceEndCullDistance != EndCullDistance)
	{
		InstanceStartCullDistance = StartCullDistance;
		InstanceEndCullDistance = EndCullDistance;

		if (GetScene() && SceneProxy)
		{
			GetScene()->UpdateInstanceCullDistance(this, StartCullDistance, EndCullDistance);
		}
	}
}

void UInstancedSkinnedMeshComponent::PreApplyComponentInstanceData(struct FInstancedSkinnedMeshComponentInstanceData* InstancedMeshData)
{
#if WITH_EDITOR
	// Prevent proxy recreate while traversing the ::ApplyToComponent stack
	bIsInstanceDataApplyCompleted = false;
#endif
}

void UInstancedSkinnedMeshComponent::ApplyComponentInstanceData(struct FInstancedSkinnedMeshComponentInstanceData* InstancedMeshData)
{
#if WITH_EDITOR
	check(InstancedMeshData);

	ON_SCOPE_EXIT
	{
		bIsInstanceDataApplyCompleted = true;
	};

	if (GetSkinnedAsset() != InstancedMeshData->SkinnedAsset)
	{
		return;
	}

	// If we should inherit from archetype do it here after data was applied and before comparing (RerunConstructionScript will serialize SkipSerialization properties and reapply them even if we want to inherit them)
	const UInstancedSkinnedMeshComponent* Archetype = Cast<UInstancedSkinnedMeshComponent>(GetArchetype());
	if (ShouldInheritPerInstanceData(Archetype))
	{
		ApplyInheritedPerInstanceData(Archetype);
	}

	SelectedInstances = InstancedMeshData->SelectedInstances;
	bHasPerInstanceHitProxies = InstancedMeshData->bHasPerInstanceHitProxies;
	PrimitiveBoundsOverride = InstancedMeshData->PrimitiveBoundsOverride;
	bIsInstanceDataGPUOnly = InstancedMeshData->bIsInstanceDataGPUOnly;
	NumInstancesGPUOnly = InstancedMeshData->NumInstancesGPUOnly;
#endif
}

FBoxSphereBounds UInstancedSkinnedMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (PrimitiveBoundsOverride.IsValid)
	{
		return PrimitiveBoundsOverride.InverseTransformBy(GetComponentTransform().Inverse() * LocalToWorld);
	}
	else
	{
		return FInstancedSkinnedMeshComponentHelper::CalcBounds(*this, LocalToWorld);
	}
}

void UInstancedSkinnedMeshComponent::SetSkinnedAssetCallback()
{
	MarkRenderStateDirty();
}

void UInstancedSkinnedMeshComponent::RefreshBoneTransforms(FActorComponentTickFunction* TickFunction)
{
	// Can't do anything without a SkinnedAsset
	if (!GetSkinnedAsset())
	{
		return;
	}

	// Do nothing more if no bones in skeleton.
	if (GetNumComponentSpaceTransforms() == 0)
	{
		return;
	}

	UpdateBounds();
	MarkRenderTransformDirty();
	MarkRenderDynamicDataDirty();
}

void UInstancedSkinnedMeshComponent::SetNumGPUInstances(int32 InCount)
{
	NumInstancesGPUOnly = InCount;
}

FPrimitiveInstanceId UInstancedSkinnedMeshComponent::AddInstance(const FTransform& InstanceTransform, int32 AnimationIndex, bool bWorldSpace)
{
	return AddInstanceInternal(InstanceData.Num(), InstanceTransform, AnimationIndex, bWorldSpace);
}

TArray<FPrimitiveInstanceId> UInstancedSkinnedMeshComponent::AddInstances(const TArray<FTransform>& Transforms, const TArray<int32>& AnimationIndices, bool bShouldReturnIds, bool bWorldSpace)
{
	TArray<FPrimitiveInstanceId> NewInstanceIds;
	if (Transforms.IsEmpty() || (Transforms.Num() != AnimationIndices.Num()))
	{
		return NewInstanceIds;
	}

	Modify();

	const int32 NumToAdd = Transforms.Num();

	if (bShouldReturnIds)
	{
		NewInstanceIds.SetNumUninitialized(NumToAdd);
	}

	// Reserve memory space
	const int32 NewNumInstances = InstanceData.Num() + NumToAdd;
	InstanceData.Reserve(NewNumInstances);
	InstanceCustomData.Reserve(NumCustomDataFloats * NewNumInstances);
#if WITH_EDITOR
	SelectedInstances.Reserve(NewNumInstances);
#endif

	for (int32 AddIndex = 0; AddIndex < NumToAdd; ++AddIndex)
	{
		const FTransform& Transform = Transforms[AddIndex];
		const int32 AnimationIndex = AnimationIndices[AddIndex];
		FPrimitiveInstanceId InstanceId = AddInstanceInternal(InstanceData.Num(), Transform, AnimationIndex, bWorldSpace);
		if (bShouldReturnIds)
		{
			NewInstanceIds[AddIndex] = InstanceId;
		}
	}

	return NewInstanceIds;
}

bool UInstancedSkinnedMeshComponent::SetCustomDataValue(FPrimitiveInstanceId InstanceId, int32 CustomDataIndex, float CustomDataValue)
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);

	if (!InstanceData.IsValidIndex(InstanceIndex) || CustomDataIndex < 0 || CustomDataIndex >= NumCustomDataFloats)
	{
		return false;
	}

	Modify();

	InstanceDataManager.CustomDataChanged(InstanceIndex);
	InstanceCustomData[InstanceIndex * NumCustomDataFloats + CustomDataIndex] = CustomDataValue;

	return true;
}

bool UInstancedSkinnedMeshComponent::SetCustomData(FPrimitiveInstanceId InstanceId, TArrayView<const float> CustomDataFloats)
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);

	if (!InstanceData.IsValidIndex(InstanceIndex) || CustomDataFloats.Num() == 0)
	{
		return false;
	}

	Modify();

	const int32 NumToCopy = FMath::Min(CustomDataFloats.Num(), NumCustomDataFloats);
	InstanceDataManager.CustomDataChanged(InstanceIndex);
	FMemory::Memcpy(&InstanceCustomData[InstanceIndex * NumCustomDataFloats], CustomDataFloats.GetData(), NumToCopy * CustomDataFloats.GetTypeSize());
	return true;
}

void UInstancedSkinnedMeshComponent::SetNumCustomDataFloats(int32 InNumCustomDataFloats)
{
	if (FMath::Max(InNumCustomDataFloats, 0) != NumCustomDataFloats)
	{
		NumCustomDataFloats = FMath::Max(InNumCustomDataFloats, 0);
	}

	if (InstanceData.Num() * NumCustomDataFloats != InstanceCustomData.Num())
	{
		InstanceDataManager.NumCustomDataChanged();

		// Clear out and reinit to 0
		InstanceCustomData.Empty(InstanceData.Num() * NumCustomDataFloats);
		InstanceCustomData.SetNumZeroed(InstanceData.Num() * NumCustomDataFloats);
	}
}

bool UInstancedSkinnedMeshComponent::GetCustomData(FPrimitiveInstanceId InstanceId, TArrayView<float> CustomDataFloats) const
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);
	if (!InstanceData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	const int32 NumToCopy = FMath::Min(CustomDataFloats.Num(), NumCustomDataFloats);
	FMemory::Memcpy(CustomDataFloats.GetData(), &InstanceCustomData[InstanceIndex * NumCustomDataFloats], NumToCopy * CustomDataFloats.GetTypeSize());
	return true;
}

bool UInstancedSkinnedMeshComponent::GetInstanceTransform(FPrimitiveInstanceId InstanceId, FTransform& OutInstanceTransform, bool bWorldSpace) const
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);
	if (!InstanceData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	const FSkinnedMeshInstanceData& Instance = InstanceData[InstanceIndex];

	OutInstanceTransform = FTransform(Instance.Transform);
	if (bWorldSpace)
	{
		OutInstanceTransform = OutInstanceTransform * GetComponentTransform();
	}

	return true;
}

bool UInstancedSkinnedMeshComponent::GetInstanceAnimationIndex(FPrimitiveInstanceId InstanceId, int32& OutAnimationIndex) const
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);
	if (!InstanceData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	OutAnimationIndex = InstanceData[InstanceIndex].AnimationIndex;
	return true;
}

bool UInstancedSkinnedMeshComponent::RemoveInstance(FPrimitiveInstanceId InstanceId)
{
	int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);
	if (InstanceData.IsValidIndex(InstanceIndex))
	{
		Modify();
		return RemoveInstanceInternal(InstanceIndex, false);
	}
	return false;
}

void UInstancedSkinnedMeshComponent::RemoveInstances(const TArray<FPrimitiveInstanceId>& InstancesToRemove)
{
	Modify();

	for (FPrimitiveInstanceId InstanceId : InstancesToRemove)
	{
		int32 InstanceIndex = InstanceDataManager.IdToIndex(InstanceId);
		RemoveInstanceInternal(InstanceIndex, false);
	}
}

void UInstancedSkinnedMeshComponent::ClearInstances()
{
	Modify();

	// Clear all the per-instance data
	InstanceData.Empty();
	InstanceCustomData.Empty();

#if WITH_EDITOR
	SelectedInstances.Empty();
#endif
	InstanceDataManager.ClearInstances();
}

struct HSkinnedMeshInstance : public HHitProxy
{
	TObjectPtr<UInstancedSkinnedMeshComponent> Component;
	int32 InstanceIndex;

	DECLARE_HIT_PROXY(ENGINE_API);
	HSkinnedMeshInstance(UInstancedSkinnedMeshComponent* InComponent, int32 InInstanceIndex)
	: HHitProxy(HPP_World)
	, Component(InComponent)
	, InstanceIndex(InInstanceIndex)
	{
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(Component);
	}

	virtual FTypedElementHandle GetElementHandle() const override
	{
	#if WITH_EDITOR
		if (Component)
		{
		#if 0
			if (true)//if (CVarEnableViewportSMInstanceSelection.GetValueOnAnyThread() != 0)
			{
				// Prefer per-instance selection if available
				// This may fail to return a handle if the feature is disabled, or if per-instance editing is disabled for this component
				if (FTypedElementHandle ElementHandle = UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(Component, InstanceIndex))
				{
					return ElementHandle;
				}
			}
		#endif

			// If per-instance selection isn't possible, fallback to general per-component selection (which may choose to select the owner actor instead)
			return UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component);
		}
	#endif	// WITH_EDITOR
		return FTypedElementHandle();
	}

	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

IMPLEMENT_HIT_PROXY(HSkinnedMeshInstance, HHitProxy);

void UInstancedSkinnedMeshComponent::CreateHitProxyData(TArray<TRefCountPtr<HHitProxy>>& HitProxies)
{
	if (GIsEditor && bHasPerInstanceHitProxies)
	{
		int32 NumProxies = InstanceData.Num();
		HitProxies.Empty(NumProxies);

		for (int32 InstanceIdx = 0; InstanceIdx < NumProxies; ++InstanceIdx)
		{
			HitProxies.Add(new HSkinnedMeshInstance(this, InstanceIdx));
		}
	}
	else
	{
		HitProxies.Empty();
	}
}

FPrimitiveInstanceId UInstancedSkinnedMeshComponent::AddInstanceInternal(int32 InstanceIndex, const FTransform& InstanceTransform, int32 AnimationIndex, bool bWorldSpace)
{
	// This happens because the editor modifies the InstanceData array _before_ callbacks. If we could change the UI to not do that we could remove this ugly hack.
	if (!InstanceData.IsValidIndex(InstanceIndex))
	{
		check(InstanceIndex == InstanceData.Num());
		InstanceData.AddDefaulted();
	}

	FPrimitiveInstanceId InstanceId = InstanceDataManager.Add(InstanceIndex);

	const FTransform3f LocalTransform = FTransform3f(bWorldSpace ? InstanceTransform.GetRelativeTransform(GetComponentTransform()) : InstanceTransform);
	SetupNewInstanceData(InstanceData[InstanceIndex], InstanceIndex, LocalTransform, AnimationIndex);

	// Add custom data to instance
	InstanceCustomData.AddZeroed(NumCustomDataFloats);

#if WITH_EDITOR
	SelectedInstances.Add(false);
#endif

	return InstanceId;
}

bool UInstancedSkinnedMeshComponent::RemoveInstanceInternal(int32 InstanceIndex, bool bInstanceAlreadyRemoved)
{
	if (!ensure(bInstanceAlreadyRemoved || InstanceData.IsValidIndex(InstanceIndex)))
	{
		return false;
	}
	InstanceDataManager.RemoveAtSwap(InstanceIndex);
	
	// remove instance
	if (!bInstanceAlreadyRemoved)
	{
		InstanceData.RemoveAtSwap(InstanceIndex, EAllowShrinking::No);
	}
	
	if (InstanceCustomData.IsValidIndex(InstanceIndex * NumCustomDataFloats))
	{
		InstanceCustomData.RemoveAtSwap(InstanceIndex * NumCustomDataFloats, NumCustomDataFloats, EAllowShrinking::No);
	}

#if WITH_EDITOR
	// remove selection flag if array is filled in
	if (SelectedInstances.IsValidIndex(InstanceIndex))
	{
		SelectedInstances.RemoveAtSwap(InstanceIndex);
	}
#endif
	return true;
}

TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> UInstancedSkinnedMeshComponent::GetOrCreateInstanceDataSceneProxy()
{
	if (bIsInstanceDataGPUOnly)
	{
		return CreateInstanceDataProxyGPUOnly();
	}
	else
	{
		return InstanceDataManager.GetOrCreateProxy();
	}
}

TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> UInstancedSkinnedMeshComponent::GetInstanceDataSceneProxy() const
{
	if (bIsInstanceDataGPUOnly)
	{
		return CreateInstanceDataProxyGPUOnly();
	}
	else
	{
		return const_cast<UInstancedSkinnedMeshComponent*>(this)->InstanceDataManager.GetProxy();
	}
}

TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> UInstancedSkinnedMeshComponent::CreateInstanceDataProxyGPUOnly() const
{
	FInstanceSceneDataBuffers InstanceSceneDataBuffers(/*InbInstanceDataIsGPUOnly=*/true);
	{
		FInstanceSceneDataBuffers::FAccessTag AccessTag(PointerHash(this));
		FInstanceSceneDataBuffers::FWriteView ProxyData = InstanceSceneDataBuffers.BeginWriteAccess(AccessTag);

		InstanceSceneDataBuffers.SetPrimitiveLocalToWorld(GetRenderMatrix(), AccessTag);

		ProxyData.NumInstancesGPUOnly = GetInstanceCountGPUOnly();
		ProxyData.NumCustomDataFloats = NumCustomDataFloats;
		ProxyData.InstanceLocalBounds.SetNum(1);
		ProxyData.InstanceLocalBounds[0] = ensure(GetSkinnedAsset()) ? GetSkinnedAsset()->GetBounds() : FBox();

		ProxyData.Flags.bHasPerInstanceCustomData = ProxyData.NumCustomDataFloats > 0;

		InstanceSceneDataBuffers.EndWriteAccess(AccessTag);
		InstanceSceneDataBuffers.ValidateData();
	}

	return MakeShared<FInstanceDataSceneProxy, ESPMode::ThreadSafe>(MoveTemp(InstanceSceneDataBuffers));
}
