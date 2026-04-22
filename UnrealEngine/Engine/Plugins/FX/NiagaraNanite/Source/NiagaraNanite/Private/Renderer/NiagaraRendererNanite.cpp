// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererNanite.h"

#include "NiagaraCullProxyComponent.h"
#include "Renderer/NiagaraNaniteRendererProperties.h"
#include "NiagaraEmitter.h"
#include "NiagaraSceneProxy.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraWorldManager.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraNaniteShaders.h"
#include "NiagaraStaticMeshComponent.h"

#include "Async/Async.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PrimitiveSceneDesc.h"
#include "SceneInterface.h"

#include "RHI.h"
#include "RHIUtilities.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

namespace NiagaraRendererNaniteLocal
{
	static bool GRendererEnabled = true;
	static FAutoConsoleVariableRef CVarRendererEnabled(
		TEXT("fx.Niagara.NaniteRenderer.Enabled"),
		GRendererEnabled,
		TEXT("If == 0, Niagara Nanite Renderers are disabled."),
		ECVF_Default
	);

	struct FUpdateInstancesContext
	{
		const UNiagaraNaniteRendererProperties*	RendererProperties = nullptr;
		FNiagaraGpuComputeDispatchInterface*	ComputeInterface = nullptr;

		int32					NumInstancesEstimate = 0;
		FNiagaraDataBufferRef	DataToRender;

		float					DefaultCustomFloats[FNiagaraNaniteGPUSceneCS::MaxCustomFloats] = {};

		FVector3f				MeshScale = FVector3f::ZeroVector;
		FQuat4f					MeshRotation = FQuat4f::Identity;

		FVector3f				DefaultPosition = FVector3f::ZeroVector;
		FQuat4f					DefaultRotation = FQuat4f::Identity;
		FVector3f				DefaultScale = FVector3f::ZeroVector;

		FVector3f				DefaultPreviousPosition = FVector3f::ZeroVector;
		FQuat4f					DefaultPreviousRotation = FQuat4f::Identity;
		FVector3f				DefaultPreviousScale = FVector3f::ZeroVector;

		int32					DefaultMeshIndex = 0;
		int32					DefaultRendererVis = 0;
		int32					MeshIndexComponentOffset = INDEX_NONE;
		int32					RendererVisComponentOffset = INDEX_NONE;

		FTransform3f			SimulationToComponent = FTransform3f::Identity;
		FTransform3f			PreviousSimulationToComponent = FTransform3f::Identity;
		FVector3f				SimulationLWCTile = FVector3f::ZeroVector;

		FRenderTransform MakeTransform(const FVector3f& Position, const FQuat4f& Rotation, const FVector3f& Scale) const
		{
			return FRenderTransform(
				FTransform3f(
					SimulationToComponent.TransformRotation(Rotation.GetNormalized()),
					SimulationToComponent.TransformPosition(Position),
					Scale
				).ToMatrixWithScale()
			);
		}

		FRenderTransform MakePreviousTransform(const FVector3f& Position, const FQuat4f& Rotation, const FVector3f& Scale) const
		{
			return FRenderTransform(
				FTransform3f(
					PreviousSimulationToComponent.TransformRotation(Rotation.GetNormalized()),
					PreviousSimulationToComponent.TransformPosition(Position),
					Scale
				).ToMatrixWithScale()
			);
		}

		void CopyInstancePerInstanceData(FInstanceSceneDataBuffers::FWriteView& ProxyData, const uint32 iInstance) const
		{
			int32 PerInstanceOffset = iInstance * RendererProperties->PerInstanceDataFloatComponents.Num();
			for (int32 iCustomFloat=0; iCustomFloat < RendererProperties->PerInstanceDataFloatComponents.Num(); ++iCustomFloat )
			{
				const int32 FloatComponent = RendererProperties->PerInstanceDataFloatComponents[iCustomFloat];
				if (FloatComponent == INDEX_NONE)
				{
					ProxyData.InstanceCustomData[PerInstanceOffset++] = DefaultCustomFloats[iCustomFloat];
				}
				else
				{
					const bool bIsHalfFloat = (FloatComponent & (1 << 31)) != 0;
					if (bIsHalfFloat)
					{
						const int32 HalfComponent = FloatComponent & ~(1 << 31);
						const FFloat16* ParticleData = reinterpret_cast<const FFloat16*>(DataToRender->GetComponentPtrHalf(HalfComponent));
						ProxyData.InstanceCustomData[PerInstanceOffset++] = ParticleData[iInstance].GetFloat();
					}
					else
					{
						const float* ParticleData = reinterpret_cast<const float*>(DataToRender->GetComponentPtrFloat(FloatComponent));
						ProxyData.InstanceCustomData[PerInstanceOffset++] = ParticleData[iInstance];
					}
				}
			}
		}

		void UpdateInstancesCPU(FInstanceSceneDataBuffers::FWriteView& ProxyData) const
		{
			if (RendererProperties->SourceMode == ENiagaraRendererSourceDataMode::Particles)
			{
				// Create all our data readers
				const auto PositionReader = RendererProperties->PositionDataSetAccessor.GetReader(DataToRender);
				const auto RotationReader = RendererProperties->RotationDataSetAccessor.GetReader(DataToRender);
				const auto ScaleReader = RendererProperties->ScaleDataSetAccessor.GetReader(DataToRender);
				const auto RendererVisReader = RendererProperties->RendererVisTagDataSetAccessor.GetReader(DataToRender);
				const auto MeshIndexReader = RendererProperties->MeshIndexDataSetAccessor.GetReader(DataToRender);
				const auto PreviousPositionReader = RendererProperties->PreviousPositionDataSetAccessor.GetReader(DataToRender);
				const auto PreviousRotationReader = RendererProperties->PreviousRotationDataSetAccessor.GetReader(DataToRender);
				const auto PreviousScaleReader = RendererProperties->PreviousScaleDataSetAccessor.GetReader(DataToRender);

				ProxyData.InstanceToPrimitiveRelative.SetNumUninitialized(DataToRender->GetNumInstances());
				ProxyData.PrevInstanceToPrimitiveRelative.SetNumUninitialized(DataToRender->GetNumInstances());
				ProxyData.InstanceCustomData.AddUninitialized(DataToRender->GetNumInstances() * RendererProperties->PerInstanceDataFloatComponents.Num());

				uint32 NumInstances = 0;
				for (uint32 iInstance = 0; iInstance < DataToRender->GetNumInstances(); ++iInstance)
				{
					const int32 RendererVis = RendererVisReader.GetSafe(iInstance, DefaultRendererVis);
					const int32 MeshIndex = MeshIndexReader.GetSafe(iInstance, DefaultMeshIndex);
					if (RendererVis != DefaultRendererVis || MeshIndex != DefaultMeshIndex)
					{
						continue;
					}

					ProxyData.InstanceToPrimitiveRelative[NumInstances] = MakeTransform(
						PositionReader.GetSafe(iInstance, DefaultPosition),
						RotationReader.GetSafe(iInstance, DefaultRotation) * MeshRotation,
						ScaleReader.GetSafe(iInstance, DefaultScale) * MeshScale
					);
					ProxyData.PrevInstanceToPrimitiveRelative[NumInstances] = MakePreviousTransform(
						PreviousPositionReader.GetSafe(iInstance, DefaultPreviousPosition),
						PreviousRotationReader.GetSafe(iInstance, DefaultPreviousRotation) * MeshRotation,
						PreviousScaleReader.GetSafe(iInstance, DefaultPreviousScale) * MeshScale
					);

					CopyInstancePerInstanceData(ProxyData, iInstance);

					++NumInstances;
				}
				ProxyData.InstanceToPrimitiveRelative.SetNumUninitialized(NumInstances, EAllowShrinking::No);
				ProxyData.PrevInstanceToPrimitiveRelative.SetNumUninitialized(NumInstances, EAllowShrinking::No);
				ProxyData.InstanceCustomData.SetNumUninitialized(NumInstances * RendererProperties->PerInstanceDataFloatComponents.Num(), EAllowShrinking::No);
			}
			else
			{
				ProxyData.InstanceToPrimitiveRelative.SetNumUninitialized(1);
				ProxyData.PrevInstanceToPrimitiveRelative.SetNumUninitialized(1);
				ProxyData.InstanceCustomData.SetNumZeroed(RendererProperties->PerInstanceDataFloatComponents.Num());

				ProxyData.InstanceToPrimitiveRelative[0] = MakeTransform(DefaultPosition, DefaultRotation, DefaultScale);
				ProxyData.PrevInstanceToPrimitiveRelative[0] = MakeTransform(DefaultPreviousPosition, DefaultPreviousRotation, DefaultPreviousScale);
				CopyInstancePerInstanceData(ProxyData, 0);
			}
		}

		void UpdateInstancesGPU(FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& GpuSceneParams) const
		{
			check(GpuSceneParams.PersistentPrimitiveId != INDEX_NONE);

			TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererProperties->RendererLayout.GetVFVariables_RenderThread();

			const int32 NumCustomFloats = FMath::Min(RendererProperties->PerInstanceDataFloatComponents.Num(), FNiagaraNaniteGPUSceneCS::MaxCustomFloats);
			const int32 NumCustomFloat4s = FMath::DivideAndRoundUp(NumCustomFloats, 4);

			FNiagaraNaniteGPUSceneCS::FParameters* Parameters = GraphBuilder.AllocParameters<FNiagaraNaniteGPUSceneCS::FParameters>();
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Parameters->GPUSceneWriterParameters	= GpuSceneParams.GPUWriteParams;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			Parameters->NumAllocatedInstances		= NumInstancesEstimate;
			Parameters->ParticleCpuCount			= DataToRender->GetNumInstances();
			Parameters->ParticleGpuCountOffset		= DataToRender->GetGPUInstanceCountBufferOffset();
			Parameters->ParticleBufferStride		= DataToRender->GetFloatStride() / sizeof(float);
			Parameters->ParticleFloatData			= FNiagaraRenderer::GetSrvOrDefaultFloat(DataToRender->GetGPUBufferFloat());
			Parameters->ParticleHalfData			= FNiagaraRenderer::GetSrvOrDefaultHalf(DataToRender->GetGPUBufferHalf());
			Parameters->ParticleIntData				= FNiagaraRenderer::GetSrvOrDefaultInt(DataToRender->GetGPUBufferInt());
			Parameters->ParticleCountBuffer			= FNiagaraRenderer::GetSrvOrDefaultUInt(ComputeInterface->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
			Parameters->NumCustomFloats				= NumCustomFloats;
			Parameters->NumCustomFloat4s			= NumCustomFloat4s;
			for (int32 i=0; i < NumCustomFloat4s; ++i)
			{
				for (int32 j = 0; j < 4; ++j)
				{
					const int32 Offset = (i * 4) + j;
					Parameters->CustomFloatComponents[i][j] = RendererProperties->PerInstanceDataFloatComponents.IsValidIndex(Offset) ? RendererProperties->PerInstanceDataFloatComponents[Offset] : INDEX_NONE;
					Parameters->DefaultCustomFloats[i][j] = DefaultCustomFloats[Offset];
				}
			}
			Parameters->PositionComponentOffset		= VFVariables[int32(ENiagaraNaniteVFLayout::Position)].GetGPUOffset();
			Parameters->RotationComponentOffset		= VFVariables[int32(ENiagaraNaniteVFLayout::Rotation)].GetGPUOffset();
			Parameters->ScaleComponentOffset		= VFVariables[int32(ENiagaraNaniteVFLayout::Scale)].GetGPUOffset();
			Parameters->PrevPositionComponentOffset	= VFVariables[int32(ENiagaraNaniteVFLayout::PrevPosition)].GetGPUOffset();		//-TODO: fallback to position if not available
			Parameters->PrevRotationComponentOffset	= VFVariables[int32(ENiagaraNaniteVFLayout::PrevRotation)].GetGPUOffset();
			Parameters->PrevScaleComponentOffset	= VFVariables[int32(ENiagaraNaniteVFLayout::PrevScale)].GetGPUOffset();
			Parameters->DefaultPosition				= DefaultPosition;
			Parameters->DefaultRotation				= DefaultRotation;
			Parameters->DefaultScale				= DefaultScale;
			Parameters->DefaultPrevPosition			= DefaultPreviousPosition;
			Parameters->DefaultPrevRotation			= DefaultPreviousRotation;
			Parameters->DefaultPrevScale			= DefaultPreviousScale;
			Parameters->MeshScale					= MeshScale;
			Parameters->MeshRotation				= MeshRotation;
			Parameters->MeshIndex					= DefaultMeshIndex;
			Parameters->RendererVis					= DefaultRendererVis;
			Parameters->MeshIndexComponentOffset	= MeshIndexComponentOffset;
			Parameters->RendererVisComponentOffset	= RendererVisComponentOffset;
			Parameters->SimulationToComponent_Translation			= SimulationToComponent.GetTranslation();
			Parameters->SimulationToComponent_Rotation				= SimulationToComponent.GetRotation();
			Parameters->SimulationToComponent_Scale					= SimulationToComponent.GetScale3D();
			Parameters->PreviousSimulationToComponent_Translation	= PreviousSimulationToComponent.GetTranslation();
			Parameters->PreviousSimulationToComponent_Rotation		= PreviousSimulationToComponent.GetRotation();
			Parameters->PreviousSimulationToComponent_Scale			= PreviousSimulationToComponent.GetScale3D();
			Parameters->SimulationLWCTile			= SimulationLWCTile;
			Parameters->PrimitiveId					= GpuSceneParams.PersistentPrimitiveId;

			TShaderMapRef<FNiagaraNaniteGPUSceneCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NiagaraNanite"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				Shader,
				Parameters,
				FComputeShaderUtils::GetGroupCountWrapped(Parameters->NumAllocatedInstances, FNiagaraNaniteGPUSceneCS::ThreadGroupSize)
			);
		}
	};
}

FNiagaraRendererNanite::FNiagaraRendererNanite(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProperties, Emitter)
{
	const UNiagaraNaniteRendererProperties* RendererProperties = CastChecked<const UNiagaraNaniteRendererProperties>(InProperties);
	MeshDatas.AddDefaulted(RendererProperties->Meshes.Num());

	const FNiagaraDataSet& ParticleDataSet = Emitter->GetParticleData();
	if (RendererProperties->MeshIndexBinding.CanBindToHostParameterMap() == false)
	{
		int32 DummyFloatOffset, DummyHalfOffset;
		ParticleDataSet.GetVariableComponentOffsets(RendererProperties->MeshIndexBinding.GetDataSetBindableVariable(), DummyFloatOffset, MeshIndexComponentOffset, DummyHalfOffset);
	}
	if (RendererProperties->RendererVisibilityTagBinding.CanBindToHostParameterMap() == false)
	{
		int32 DummyFloatOffset, DummyHalfOffset;
		ParticleDataSet.GetVariableComponentOffsets(RendererProperties->RendererVisibilityTagBinding.GetDataSetBindableVariable(), DummyFloatOffset, RendererVisComponentOffset, DummyHalfOffset);
	}
}

FNiagaraRendererNanite::~FNiagaraRendererNanite()
{
	ReleaseComponents();
}

void FNiagaraRendererNanite::Initialize(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* InEmitter, const FNiagaraSystemInstanceController& InController)
{
	FNiagaraRenderer::Initialize(InProperties, InEmitter, InController);

	const UNiagaraNaniteRendererProperties* RendererProperties = CastChecked<const UNiagaraNaniteRendererProperties>(InProperties);

	for ( int32 iMesh=0; iMesh < MeshDatas.Num(); ++iMesh )
	{
		const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties = RendererProperties->Meshes[iMesh];
		FMeshData& MeshData		= MeshDatas[iMesh];
		MeshData.Scale			= MeshProperties.Scale;
		MeshData.Rotation		= MeshProperties.Rotation.Quaternion();
		//MeshData.PivotOffset	= MeshProperties.PivotOffset;

		if (UStaticMesh* ResolvedMesh = MeshProperties.GetResolvedMesh(InEmitter))
		{
			TArray<UMaterialInterface*> UsedMaterials;
			RendererProperties->GetMeshUsedMaterials(MeshProperties, InEmitter, UsedMaterials);
			RendererProperties->ApplyMaterialOverrides(InEmitter, UsedMaterials);

			for (UMaterialInterface* UsedMaterial : UsedMaterials)
			{
				MeshData.MaterialRemapTable.Add(
					BaseMaterials_GT.IndexOfByPredicate(
						[&](UMaterialInterface* LookMat)
						{
							if (LookMat == UsedMaterial)
							{
								return true;
							}
							if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(LookMat))
							{
								return UsedMaterial == MID->Parent;
							}
							return false;
						}
					)
				);
			}
		}
	}
}

void FNiagaraRendererNanite::ReleaseComponents()
{
	// Simple path we can destroy the components inline
	if (IsInGameThread())
	{
		for (int32 iMesh = 0; iMesh < MeshDatas.Num(); ++iMesh)
		{
			FMeshData& MeshData = MeshDatas[iMesh];
			if (UNiagaraStaticMeshComponent* MeshComponent = MeshData.MeshComponent.Get())
			{
				MeshComponent->DestroyComponent();
				MeshData.MeshComponent = nullptr;
			}
		}
	}
	// Run task to destroy the component
	else
	{
		TArray<TWeakObjectPtr<UNiagaraStaticMeshComponent>, TInlineAllocator<4>> ComponentsToDestroy;
		ComponentsToDestroy.Reserve(MeshDatas.Num());
		for (int32 iMesh = 0; iMesh < MeshDatas.Num(); ++iMesh)
		{
			FMeshData& MeshData = MeshDatas[iMesh];
			if (UNiagaraStaticMeshComponent* MeshComponent = MeshData.MeshComponent.Get())
			{
				ComponentsToDestroy.Add(MeshComponent);
				MeshData.MeshComponent = nullptr;
			}
		}

		if (ComponentsToDestroy.Num() > 0)
		{
			AsyncTask(
				ENamedThreads::GameThread,
				[ComponentArray = MoveTemp(ComponentsToDestroy)]()
				{
					for (TWeakObjectPtr<USceneComponent> WeakComponent : ComponentArray)
					{
						if (USceneComponent* Component = WeakComponent.Get())
						{
							Component->DestroyComponent();
						}
					}
				}
			);
		}
	}
}


void FNiagaraRendererNanite::PostSystemTick_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)
{
	using namespace NiagaraRendererNaniteLocal;
	
	// Get Particle Data
	const FNiagaraDataSet& DataSet = Emitter->GetParticleData();
	FNiagaraDataBuffer* DataToRender = DataSet.GetCurrentData();
	FNiagaraSystemInstance* SystemInstance = Emitter->GetParentSystemInstance();
	USceneComponent* OwnerComponent = SystemInstance ? SystemInstance->GetAttachComponent() : nullptr;
	if (DataToRender == nullptr || OwnerComponent == nullptr || !IsRendererEnabled(InProperties, Emitter))
	{
		ReleaseComponents();
		return;
	}

#if WITH_EDITORONLY_DATA
	// Handle isolation when in the editor
	if (Emitter->IsDisabledFromIsolation())
	{
		ReleaseComponents();
		return;
	}
#endif

	// Get the number of particles active
	// GetNumParticles is conservative for the GPU side as we need to understand our instance count locally
	const UNiagaraNaniteRendererProperties* RendererProperties = CastChecked<const UNiagaraNaniteRendererProperties>(InProperties);
	const int32 NumInstancesEstimate = (RendererProperties->SourceMode == ENiagaraRendererSourceDataMode::Particles) ? Emitter->GetNumParticles() : 1;
	if (NumInstancesEstimate == 0)
	{
		ReleaseComponents();
		return;
	}

	// Process material parameters
	if (RendererProperties->MaterialParameters.HasAnyBindings())
	{
		ProcessMaterialParameterBindings(RendererProperties->MaterialParameters, Emitter, MakeArrayView(BaseMaterials_GT));
	}

	// Setup UpdateContext
	const bool bUseLocalSpace = Emitter->IsLocalSpace();
	const bool bUpdateInstancesOnCPU = SimTarget == ENiagaraSimTarget::CPUSim || RendererProperties->SourceMode != ENiagaraRendererSourceDataMode::Particles;

	const FNiagaraParameterStore& ParameterStore = Emitter->GetRendererBoundVariables();
	const FNiagaraPosition DefaultPos = FVector::ZeroVector;
	const FQuat4f DefaultRot = FQuat4f::Identity;
	const FVector3f DefaultScale = FVector3f::OneVector;

	FUpdateInstancesContext UpdateContext;
	UpdateContext.RendererProperties		= RendererProperties;
	UpdateContext.NumInstancesEstimate		= NumInstancesEstimate;
	UpdateContext.DataToRender				= DataToRender;
	UpdateContext.ComputeInterface			= SimTarget == ENiagaraSimTarget::GPUComputeSim ? FNiagaraGpuComputeDispatchInterface::Get(SystemInstance->GetWorld()) : nullptr;
	for (int32 i = 0; i < RendererProperties->PerInstanceDataBindings.Num(); ++i)
	{
		const FNiagaraFloatParameterComponentBinding& ComponentBinding = RendererProperties->PerInstanceDataBindings[i];
		const float* ParameterData = reinterpret_cast<const float*>(ParameterStore.GetParameterData(ComponentBinding.ResolvedParameter));
		UpdateContext.DefaultCustomFloats[i] = ParameterData ? ParameterData[ComponentBinding.Component] : ComponentBinding.DefaultValue;
	}
	UpdateContext.DefaultPosition			= ParameterStore.GetParameterValueOrDefault(RendererProperties->PositionBinding.GetParamMapBindableVariable(), DefaultPos);
	UpdateContext.DefaultRotation			= ParameterStore.GetParameterValueOrDefault(RendererProperties->RotationBinding.GetParamMapBindableVariable(), DefaultRot);
	UpdateContext.DefaultScale				= ParameterStore.GetParameterValueOrDefault(RendererProperties->ScaleBinding.GetParamMapBindableVariable(), DefaultScale);
	UpdateContext.DefaultPreviousPosition	= ParameterStore.GetParameterValueOrDefault(RendererProperties->PreviousPositionBinding.GetParamMapBindableVariable(), DefaultPos);
	UpdateContext.DefaultPreviousRotation	= ParameterStore.GetParameterValueOrDefault(RendererProperties->PreviousRotationBinding.GetParamMapBindableVariable(), DefaultRot);
	UpdateContext.DefaultPreviousScale		= ParameterStore.GetParameterValueOrDefault(RendererProperties->PreviousScaleBinding.GetParamMapBindableVariable(), DefaultScale);
	UpdateContext.DefaultRendererVis		= ParameterStore.GetParameterValueOrDefault(RendererProperties->RendererVisibilityTagBinding.GetParamMapBindableVariable(), RendererProperties->RendererVisibility);
	UpdateContext.MeshIndexComponentOffset	= MeshIndexComponentOffset;
	UpdateContext.RendererVisComponentOffset = RendererVisComponentOffset;
	UpdateContext.SimulationLWCTile			= SystemInstance->GetLWCTile();

	// GPU instances are expected to be in LWC space, CPU instances are relative to the component
	if (bUpdateInstancesOnCPU)
	{
		UpdateContext.SimulationToComponent			= bUseLocalSpace ? FTransform3f::Identity : FTransform3f(SystemInstance->GetWorldTransform().Inverse());
		UpdateContext.PreviousSimulationToComponent = bUseLocalSpace ? FTransform3f::Identity : FTransform3f(SystemInstance->GetWorldTransform().Inverse());
	}
	else
	{
		UpdateContext.SimulationToComponent			= bUseLocalSpace ? FTransform3f(SystemInstance->GetWorldTransform()) : FTransform3f::Identity;
		UpdateContext.PreviousSimulationToComponent	= bUseLocalSpace ? FTransform3f(SystemInstance->GetWorldTransform()) : FTransform3f::Identity;
	}

	const TOptional<int32> DefaultMeshIndex	= ParameterStore.GetParameterOptionalValue<int32>(RendererProperties->MeshIndexBinding.GetParamMapBindableVariable());

	// GetNumParticles is conservative and will be > GetNumInstances() which for GPU might change
	const int32 NumInstances = (RendererProperties->SourceMode == ENiagaraRendererSourceDataMode::Particles) ? Emitter->GetNumParticles()/*DataToRender->GetNumInstances()*/ : 1;
	for (int32 iMesh=0; iMesh < MeshDatas.Num(); ++iMesh)
	{
		FMeshData& MeshData = MeshDatas[iMesh];
		UNiagaraStaticMeshComponent* MeshComponent = MeshData.MeshComponent.Get();

		// Check for none particle vis tag / mesh index
		if ( (UpdateContext.DefaultRendererVis != RendererProperties->RendererVisibility) || (DefaultMeshIndex.Get(iMesh) != iMesh) )
		{
			if (MeshComponent)
			{
				MeshComponent->SetStaticMesh(nullptr);
			}
			continue;
		}
		UpdateContext.DefaultMeshIndex	= iMesh;
		UpdateContext.MeshScale			= MeshData.Scale;
		UpdateContext.MeshRotation		= MeshData.Rotation;

		//-TODO: For CPU simulations we could add a MeshIndex/RendererVis pass in to quickly reject

		// Resolve the mesh
		{
			UStaticMesh* ResolvedMesh = RendererProperties->Meshes[iMesh].GetResolvedMesh(Emitter);
			UStaticMesh* ExistingMesh = MeshComponent ? MeshComponent->GetStaticMesh().Get() : nullptr;
			if (ExistingMesh != ResolvedMesh)
			{
				// We can release this component
				if (ResolvedMesh == nullptr)
				{
					if (MeshComponent)
					{
						MeshComponent->SetStaticMesh(nullptr);
					}
					continue;
				}

				// Do we need to create a component?
				if (MeshComponent == nullptr)
				{
					MeshComponent = NewObject<UNiagaraStaticMeshComponent>(OwnerComponent);
					MeshComponent->SetFlags(RF_Transient);
					MeshComponent->SetMobility(EComponentMobility::Movable);
					MeshComponent->SetStaticMesh(ResolvedMesh);
					MeshComponent->SetupAttachment(OwnerComponent);
					MeshComponent->RegisterComponentWithWorld(OwnerComponent->GetWorld());
					MeshComponent->bUseCpuOnlyUpdates = SimTarget == ENiagaraSimTarget::CPUSim || RendererProperties->SourceMode != ENiagaraRendererSourceDataMode::Particles;
					MeshComponent->NumCustomDataFloats = RendererProperties->PerInstanceDataFloatComponents.Num();
					if ( UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(OwnerComponent) )
					{
						MeshComponent->CastShadow						= PrimitiveComponent->CastShadow;
						MeshComponent->bReceivesDecals					= PrimitiveComponent->bReceivesDecals;
						MeshComponent->bRenderCustomDepth				= PrimitiveComponent->bRenderCustomDepth;
						MeshComponent->CustomDepthStencilWriteMask		= PrimitiveComponent->CustomDepthStencilWriteMask;
						MeshComponent->CustomDepthStencilValue			= PrimitiveComponent->CustomDepthStencilValue;
						MeshComponent->TranslucencySortPriority			= PrimitiveComponent->TranslucencySortPriority;
						MeshComponent->TranslucencySortDistanceOffset	= PrimitiveComponent->TranslucencySortDistanceOffset;
					}
					MeshComponent->Activate();
					MeshComponent->AddTickPrerequisiteComponent(OwnerComponent);
					for (int32 iMaterial = 0; iMaterial < MeshData.MaterialRemapTable.Num(); ++iMaterial)
					{
						const int32 RemapIndex = MeshData.MaterialRemapTable[iMaterial];
						MeshComponent->SetMaterial(iMaterial, BaseMaterials_GT.IsValidIndex(RemapIndex) ? BaseMaterials_GT[RemapIndex] : nullptr);
					}

					MeshData.MeshComponent = MeshComponent;
				}
				else
				{
					MeshComponent->SetStaticMesh(ResolvedMesh);
				}
			}
		}

		// Anything to render with?
		if ( MeshComponent == nullptr || MeshComponent->GetStaticMesh() == nullptr )
		{
			continue;
		}

		if (bUpdateInstancesOnCPU)
		{
			MeshComponent->UpdateInstanceCPU(
				UpdateContext.NumInstancesEstimate,
				[UpdateContext](FInstanceSceneDataBuffers::FWriteView& ProxyData)
				{
					UpdateContext.UpdateInstancesCPU(ProxyData);
				}
			);
		}
		else
		{
			MeshComponent->UpdateInstanceGPU(
				UpdateContext.NumInstancesEstimate,
				[UpdateContext](FRDGBuilder& GraphBuilder, const FGPUSceneWriteDelegateParams& GpuSceneParams)
				{
					UpdateContext.UpdateInstancesGPU(GraphBuilder, GpuSceneParams);
				}
			);
		}
	}
}

void FNiagaraRendererNanite::OnSystemComplete_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter)
{
	ReleaseComponents();
}

int32 FNiagaraRendererNanite::GetMaxCustomFloats()
{
	return FNiagaraNaniteGPUSceneCS::MaxCustomFloats;
}
