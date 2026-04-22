// Copyright Epic Games, Inc. All Rights Reserved.

#include "Renderer/NiagaraNaniteRendererProperties.h"
#include "NiagaraBoundsCalculatorHelper.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraRenderer.h"
#include "NiagaraRendererNanite.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "MaterialDomain.h"
#include "StaticMeshResources.h"

#if WITH_EDITOR
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWidget.h"
#include "Styling/SlateIconFinder.h"
#include "AssetThumbnail.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraNaniteRendererProperties)

#define LOCTEXT_NAMESPACE "UNiagaraNaniteRendererProperties"

namespace NiagaraNaniteRendererPropertiesLocal
{
	TArray<TWeakObjectPtr<UNiagaraNaniteRendererProperties>> RendererPropertiesToDeferredInit;

	static void SetupBindings(UNiagaraNaniteRendererProperties* Props)
	{
		if (Props->PositionBinding.IsValid())
		{
			return;
		}
		Props->PositionBinding				= FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		Props->RotationBinding				= FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MESH_ORIENTATION);
		Props->ScaleBinding					= FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SCALE);
		Props->RendererVisibilityTagBinding	= FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VISIBILITY_TAG);
		Props->MeshIndexBinding				= FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MESH_INDEX);
		
		FVersionedNiagaraEmitterBase OwnerEmitter = Props->GetOuterEmitterBase();
		Props->UpdatePreviousBindings(OwnerEmitter);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FNiagaraNaniteMaterialOverride::FNiagaraNaniteMaterialOverride()
	: UserParamBinding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()))
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FNiagaraNaniteMeshRendererMeshProperties::FNiagaraNaniteMeshRendererMeshProperties()
{
#if WITH_EDITORONLY_DATA
	MeshParameterBinding.SetUsage(ENiagaraParameterBindingUsage::NotParticle);
	MeshParameterBinding.SetAllowedObjects({ UStaticMesh::StaticClass() });
#endif
}

UStaticMesh* FNiagaraNaniteMeshRendererMeshProperties::GetResolvedMesh(const FNiagaraEmitterInstance* InEmitter) const
{
	if (InEmitter && MeshParameterBinding.ResolvedParameter.IsValid())
	{
		return Cast<UStaticMesh>(InEmitter->GetRendererBoundVariables().GetUObject(MeshParameterBinding.ResolvedParameter));
	}
	return Mesh.Get();
}

UNiagaraNaniteRendererProperties::UNiagaraNaniteRendererProperties()
{
	AttributeBindings =
	{
		&PositionBinding,
		&RotationBinding,
		&ScaleBinding,
		&PreviousPositionBinding,
		&PreviousRotationBinding,
		&PreviousScaleBinding,
		&RendererVisibilityTagBinding,
		&MeshIndexBinding,
	};
}

void UNiagaraNaniteRendererProperties::PostLoad()
{
	Super::PostLoad();
	
	PostLoadBindings(SourceMode);
	MaterialParameters.ConditionalPostLoad();
}

void UNiagaraNaniteRendererProperties::PostInitProperties()
{
	using namespace NiagaraNaniteRendererPropertiesLocal;

	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			NiagaraNaniteRendererPropertiesLocal::RendererPropertiesToDeferredInit.Add(this);
			return;
		}
		SetupBindings(this);
	}
}

void UNiagaraNaniteRendererProperties::Serialize(FArchive& Ar)
{
	//-TODO:
	// MIC will replace the main material during serialize
	// Be careful if adding code that looks at the material to make sure you get the correct one
	{
//#if WITH_EDITORONLY_DATA
//		TOptional<TGuardValue<TObjectPtr<UMaterialInterface>>> MICGuard;
//		if (Ar.IsSaving() && Ar.IsCooking() && MICMaterial)
//		{
//			MICGuard.Emplace(Material, MICMaterial);
//		}
//#endif

		Super::Serialize(Ar);
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraNaniteRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraNaniteRendererProperties, SourceMode))
	{
		UpdateSourceModeDerivates(SourceMode, true);
	}

	// Update our MICs if we change material / material bindings
	//-OPT: Could narrow down further to only static materials
	if ((MemberPropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraNaniteRendererProperties, OverrideMaterials)) ||
		(MemberPropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraNaniteRendererProperties, Meshes)) ||
		(MemberPropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraNaniteRendererProperties, MaterialParameters)) )
	{
		UpdateMICs();
	}
}
#endif// WITH_EDITORONLY_DATA

void UNiagaraNaniteRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	using namespace NiagaraNaniteRendererPropertiesLocal;

	UNiagaraNaniteRendererProperties* CDO = CastChecked<UNiagaraNaniteRendererProperties>(UNiagaraNaniteRendererProperties::StaticClass()->GetDefaultObject());
	SetupBindings(CDO);

	for (TWeakObjectPtr<UNiagaraNaniteRendererProperties>& WeakProps : NiagaraNaniteRendererPropertiesLocal::RendererPropertiesToDeferredInit)
	{
		if (UNiagaraNaniteRendererProperties* Props = WeakProps.Get())
		{
			SetupBindings(Props);
		}
	}
}

FNiagaraRenderer* UNiagaraNaniteRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController)
{
	FNiagaraRenderer* NewRenderer = new FNiagaraRendererNanite(FeatureLevel, this, Emitter);
	NewRenderer->Initialize(this, Emitter, InController);
	return NewRenderer;
}

FNiagaraBoundsCalculator* UNiagaraNaniteRendererProperties::CreateBoundsCalculator()
{
	FBox LocalBounds;
	LocalBounds.Init();

	FVector MaxLocalMeshOffset(ForceInitToZero);
	FVector MaxWorldMeshOffset(ForceInitToZero);

	bool bLocalSpace = false;
	if (FVersionedNiagaraEmitterData* EmitterData = GetEmitterData())
	{
		bLocalSpace = EmitterData->bLocalSpace;
	}

	for (const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties : Meshes)
	{
		UStaticMesh* ResolvedMesh = MeshProperties.GetResolvedMesh(nullptr);
		if (ResolvedMesh == nullptr)
		{
			continue;
		}

		FBox MeshBounds = MeshProperties.Mesh->GetBounds().GetBox();
		MeshBounds.Min *= FVector(MeshProperties.Scale);
		MeshBounds.Max *= FVector(MeshProperties.Scale);

		LocalBounds += MeshBounds;
	}

	if (LocalBounds.IsValid)
	{
		// Take the bounding center into account with the extents, as it may not be at the origin
		const FVector Extents = LocalBounds.Max.GetAbs().ComponentMax(LocalBounds.Min.GetAbs());
		return new FNiagaraBoundsCalculatorHelper<false, true, false>(Extents, MaxLocalMeshOffset, MaxWorldMeshOffset, bLocalSpace);
	}
	return nullptr;
}

void UNiagaraNaniteRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	TArray<UMaterialInterface*> OrderedMeshMaterials;
	for (const FNiagaraNaniteMeshRendererMeshProperties& Mesh : Meshes)
	{
		OrderedMeshMaterials.Reset(0);
		GetMeshUsedMaterials(Mesh, InEmitter, OrderedMeshMaterials);
		if (OrderedMeshMaterials.Num() > 0)
		{
			ApplyMaterialOverrides(InEmitter, OrderedMeshMaterials);

			OutMaterials.Reserve(OutMaterials.Num() + OrderedMeshMaterials.Num());
			for (TObjectPtr<UMaterialInterface> MaterialInterface : OrderedMeshMaterials)
			{
				if (MaterialInterface)
				{
					OutMaterials.AddUnique(MaterialInterface);
				}
			}
		}
	}
}

void UNiagaraNaniteRendererProperties::CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData)
{
	UpdateSourceModeDerivates(SourceMode);
	UpdateMICs();

	// Initialize layout
	RendererLayout.Initialize(int32(ENiagaraNaniteVFLayout::Max));
	RendererLayout.SetVariableFromBinding(CompiledData, PositionBinding, int32(ENiagaraNaniteVFLayout::Position));
	RendererLayout.SetVariableFromBinding(CompiledData, RotationBinding, int32(ENiagaraNaniteVFLayout::Rotation));
	RendererLayout.SetVariableFromBinding(CompiledData, ScaleBinding, int32(ENiagaraNaniteVFLayout::Scale));
	RendererLayout.SetVariableFromBinding(CompiledData, PreviousPositionBinding, int32(ENiagaraNaniteVFLayout::PrevPosition));
	RendererLayout.SetVariableFromBinding(CompiledData, PreviousRotationBinding, int32(ENiagaraNaniteVFLayout::PrevRotation));
	RendererLayout.SetVariableFromBinding(CompiledData, PreviousScaleBinding, int32(ENiagaraNaniteVFLayout::PrevScale));
	RendererLayout.Finalize();

	// Initialize direct accessors
	InitParticleDataSetAccessor(PositionDataSetAccessor,		CompiledData, PositionBinding);
	InitParticleDataSetAccessor(RotationDataSetAccessor,		CompiledData, RotationBinding);
	InitParticleDataSetAccessor(ScaleDataSetAccessor,			CompiledData, ScaleBinding);
	if (NeedsPreciseMotionVectors())
	{
		InitParticleDataSetAccessor(PreviousPositionDataSetAccessor,	CompiledData, PreviousPositionBinding);
		InitParticleDataSetAccessor(PreviousRotationDataSetAccessor,	CompiledData, PreviousRotationBinding);
		InitParticleDataSetAccessor(PreviousScaleDataSetAccessor,		CompiledData, PreviousScaleBinding);
	}
	else
	{
		InitParticleDataSetAccessor(PreviousPositionDataSetAccessor,	CompiledData, PositionBinding);
		InitParticleDataSetAccessor(PreviousRotationDataSetAccessor,	CompiledData, RotationBinding);
		InitParticleDataSetAccessor(PreviousScaleDataSetAccessor,		CompiledData, ScaleBinding);
	}
	InitParticleDataSetAccessor(RendererVisTagDataSetAccessor,	CompiledData, RendererVisibilityTagBinding);
	InitParticleDataSetAccessor(MeshIndexDataSetAccessor,		CompiledData, MeshIndexBinding);

	// Accessors for per instance data
	PerInstanceDataFloatComponents.Empty(PerInstanceDataBindings.Num());
	for (const FNiagaraFloatParameterComponentBinding& ComponentBinding : PerInstanceDataBindings)
	{
		int32 ComponentIndex = INDEX_NONE;
		if (CompiledData && ComponentBinding.ResolvedParameter.IsValid())
		{
			FNiagaraVariableBase ParticleVariable = ComponentBinding.ResolvedParameter;
			if (ParticleVariable.RemoveRootNamespace(FNiagaraConstants::ParticleAttributeNamespaceString))
			{
				const FNiagaraRendererVariableInfo VariableInfo(CompiledData, ParticleVariable);
				if (VariableInfo.GetRawDatasetOffset() != INDEX_NONE)
				{
					ComponentIndex = VariableInfo.GetEncodedDatasetOffset() + ComponentBinding.Component;
				}
			}
		}

		PerInstanceDataFloatComponents.Add(ComponentIndex);
	}
}

void UNiagaraNaniteRendererProperties::UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit)
{
	Super::UpdateSourceModeDerivates(InSourceMode, bFromPropertyEdit);

	FVersionedNiagaraEmitterBase OwnerEmitter = GetOuterEmitterBase();
	UpdatePreviousBindings(OwnerEmitter);

	for (FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
	{
		MaterialParamBinding.CacheValues(OwnerEmitter.Emitter);
	}
}

bool UNiagaraNaniteRendererProperties::PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore)
{
	bool bAnyAdded = Super::PopulateRequiredBindings(InParameterStore);

	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		if (Binding && Binding->CanBindToHostParameterMap())
		{
			InParameterStore.AddParameter(Binding->GetParamMapBindableVariable(), false);
			bAnyAdded = true;
		}
	}

	for (FNiagaraNaniteMeshRendererMeshProperties& Mesh : Meshes)
	{
		if (Mesh.MeshParameterBinding.ResolvedParameter.IsValid())
		{
			InParameterStore.AddParameter(Mesh.MeshParameterBinding.ResolvedParameter, false);
			bAnyAdded = true;
		}
	}

	for (const FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
	{
		InParameterStore.AddParameter(MaterialParamBinding.GetParamMapBindableVariable(), false);
		bAnyAdded = true;
	}

	for (const FNiagaraFloatParameterComponentBinding& PerInstanceBinding : PerInstanceDataBindings)
	{
		if (PerInstanceBinding.ResolvedParameter.IsValid())
		{
			InParameterStore.AddParameter(PerInstanceBinding.ResolvedParameter, false);
			bAnyAdded = true;
		}
	}

	return bAnyAdded;
}

#if WITH_EDITORONLY_DATA
const TArray<FNiagaraVariable>& UNiagaraNaniteRendererProperties::GetOptionalAttributes()
{
	using namespace NiagaraNaniteRendererPropertiesLocal;

	static TArray<FNiagaraVariable> Attrs;
	if (Attrs.Num() == 0)
	{
		Attrs =
		{
			SYS_PARAM_PARTICLES_POSITION,
			SYS_PARAM_PARTICLES_MESH_ORIENTATION,
			SYS_PARAM_PARTICLES_SCALE,
			SYS_PARAM_PARTICLES_VISIBILITY_TAG,
			SYS_PARAM_PARTICLES_MESH_INDEX,
		};
	}
	return Attrs;
}

void UNiagaraNaniteRendererProperties::GetAdditionalVariables(TArray<FNiagaraVariableBase>& OutArray) const
{
	if (NeedsPreciseMotionVectors())
	{
		OutArray.Reserve(OutArray.Num() + 3);
		OutArray.AddUnique(PreviousPositionBinding.GetParamMapBindableVariable());
		OutArray.AddUnique(PreviousRotationBinding.GetParamMapBindableVariable());
		OutArray.AddUnique(PreviousScaleBinding.GetParamMapBindableVariable());
	}
}

void UNiagaraNaniteRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TArray<UObject*> Assets;
	// We create a thumbnail per mesh entry even if it's invalid to highlight that there are unused mesh entries
	for (const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties : Meshes)
	{
		UStaticMesh* Mesh = MeshProperties.Mesh;
		Assets.Add(Mesh && Mesh->HasValidRenderData() ? Mesh : nullptr);
	}

	CreateRendererWidgetsForAssets(Assets, InThumbnailPool, OutWidgets);
}

void UNiagaraNaniteRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	for (const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties : Meshes)
	{
		UStaticMesh* Mesh = MeshProperties.Mesh;
		OutWidgets.Add(SNew(STextBlock).Text(LOCTEXT("NaniteRenderer", "Nanite Renderer")));
	}
}

//const FSlateBrush* UNiagaraNaniteRendererProperties::GetStackIcon() const
//{
//	return FSlateIconFinder::FindIconBrushForClass(UNiagaraNaniteRendererProperties::StaticClass());
//}

void UNiagaraNaniteRendererProperties::GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FNiagaraRendererFeedback>& OutErrors, TArray<FNiagaraRendererFeedback>& OutWarnings, TArray<FNiagaraRendererFeedback>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);

	if (MaterialParameters.HasAnyBindings())
	{
		TArray<UMaterialInterface*> Materials;
		GetUsedMaterials(nullptr, Materials);
		MaterialParameters.GetFeedback(Materials, OutWarnings);
	}

	const int32 MaxCustomFloats = FNiagaraRendererNanite::GetMaxCustomFloats();
	const int32 NumCustomFloats = PerInstanceDataBindings.Num();
	if (NumCustomFloats > MaxCustomFloats)
	{
		OutWarnings.Emplace(
			FText::Format(LOCTEXT("MaxCustomFloatsDesc", "The maximum number of custom floats {0} has been exceeded by {1}.  Data will be lost which may result in incorrect rendering, either in case the limit in code or reduce the amount of data sent."), FText::AsNumber(MaxCustomFloats), FText::AsNumber(NumCustomFloats - MaxCustomFloats)),
			LOCTEXT("MaxCustomFloatsTitle", "The maximum number of custom floats has been exceeded")
		);
	}
}

TArray<FNiagaraVariable> UNiagaraNaniteRendererProperties::GetBoundAttributes() const
{
	TArray<FNiagaraVariable> BoundAttributes = Super::GetBoundAttributes();
	BoundAttributes.Reserve(BoundAttributes.Num() + MaterialParameters.AttributeBindings.Num() + PerInstanceDataBindings.Num());

	for (const FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
	{
		BoundAttributes.AddUnique(MaterialParamBinding.GetParamMapBindableVariable());
	}

	for (const FNiagaraFloatParameterComponentBinding& ComponentBinding : PerInstanceDataBindings)
	{
		if (ComponentBinding.ResolvedParameter.IsValid())
		{
			BoundAttributes.AddUnique(ComponentBinding.ResolvedParameter);
		}
	}

	return BoundAttributes;
}

void UNiagaraNaniteRendererProperties::RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const FVersionedNiagaraEmitterBase& InEmitter)
{
	Super::RenameVariable(OldVariable, NewVariable, InEmitter);
#if WITH_EDITORONLY_DATA
	MaterialParameters.RenameVariable(OldVariable, NewVariable, InEmitter, GetCurrentSourceMode());

	const FString EmitterName = InEmitter.Emitter ? InEmitter.Emitter->GetUniqueEmitterName() : FString();
	for (FNiagaraFloatParameterComponentBinding& Binding : PerInstanceDataBindings)
	{
		Binding.OnRenameVariable(OldVariable, NewVariable, EmitterName);
	}
#endif
}

void UNiagaraNaniteRendererProperties::RemoveVariable(const FNiagaraVariableBase& OldVariable, const FVersionedNiagaraEmitterBase& InEmitter)
{
	Super::RemoveVariable(OldVariable, InEmitter);
#if WITH_EDITORONLY_DATA
	MaterialParameters.RemoveVariable(OldVariable, InEmitter, GetCurrentSourceMode());

	const FString EmitterName = InEmitter.Emitter ? InEmitter.Emitter->GetUniqueEmitterName() : FString();
	for (FNiagaraFloatParameterComponentBinding& Binding : PerInstanceDataBindings)
	{
		Binding.OnRemoveVariable(OldVariable, EmitterName);
	}
#endif
}

void UNiagaraNaniteRendererProperties::RenameEmitter(const FName& InOldName, const UNiagaraEmitter* InRenamedEmitter)
{
	Super::RenameEmitter(InOldName, InRenamedEmitter);

	if (InRenamedEmitter)
	{
		for (FNiagaraFloatParameterComponentBinding& Binding : PerInstanceDataBindings)
		{
			Binding.OnRenameEmitter(InRenamedEmitter->GetUniqueEmitterName());
		}
	}
}
#endif // WITH_EDITORONLY_DATA

void UNiagaraNaniteRendererProperties::GetMeshUsedMaterials(const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties, const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	const UStaticMesh* StaticMesh = MeshProperties.GetResolvedMesh(InEmitter);
	const FStaticMeshRenderData* RenderData = StaticMesh ? StaticMesh->GetRenderData() : nullptr;
	if (RenderData == nullptr )
	{
		return;
	}

	for (const FStaticMeshLODResources& LODModel : RenderData->LODResources)
	{
		for (const FStaticMeshSection& Section : LODModel.Sections)
		{
			if (Section.MaterialIndex >= 0)
			{
				if (Section.MaterialIndex >= OutMaterials.Num())
				{
					OutMaterials.AddZeroed(Section.MaterialIndex - OutMaterials.Num() + 1);
				}
				else if (OutMaterials[Section.MaterialIndex])
				{
					continue;
				}

				UMaterialInterface* Material = StaticMesh->GetMaterial(Section.MaterialIndex);
				if (!Material)
				{
					Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}
				OutMaterials[Section.MaterialIndex] = Material;
			}
		}
	}
}

void UNiagaraNaniteRendererProperties::UpdatePreviousBindings(const FVersionedNiagaraEmitterBase& Emitter)
{
	//-TODO: Clean this up, failure comes from PostInitProperties
	if (!HasAnyFlags(RF_NeedPostLoad) && (Emitter.Emitter && !Emitter.Emitter->HasAnyFlags(RF_NeedLoad)))
	{
		PreviousPositionBinding.SetAsPreviousValue(PositionBinding, Emitter, SourceMode);
		PreviousRotationBinding.SetAsPreviousValue(RotationBinding, Emitter, SourceMode);
		PreviousScaleBinding.SetAsPreviousValue(ScaleBinding, Emitter, SourceMode);
	}
}

void UNiagaraNaniteRendererProperties::UpdateMICs()
{
#if WITH_EDITORONLY_DATA
	// Grab existing MICs so we can reuse and clear them out so they aren't applied during GetUsedMaterials
	TArray<TObjectPtr<UMaterialInstanceConstant>> MICMaterials;
	MICMaterials.Reserve(MICOverrideMaterials.Num());
	for (const FNiagaraNaniteMICOverride& ExistingOverride : MICOverrideMaterials)
	{
		MICMaterials.Add(ExistingOverride.ReplacementMaterial);
	}
	MICOverrideMaterials.Reset(0);

	// Gather materials and generate MICs
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(nullptr, Materials);

	UpdateMaterialParametersMIC(MaterialParameters, Materials, MICMaterials);

	// Create Material <-> MIC remap
	for (int i = 0; i < MICMaterials.Num(); ++i)
	{
		const FNiagaraNaniteMICOverride* ExistingOverride = MICOverrideMaterials.FindByPredicate([FindMaterial = Materials[i]](const FNiagaraNaniteMICOverride& ExistingOverride) { return ExistingOverride.OriginalMaterial == FindMaterial; });
		if (ExistingOverride)
		{
			ensureMsgf(ExistingOverride->ReplacementMaterial == MICMaterials[i], TEXT("MIC Material should match replacement material, static bindings will be incorrect.  Please report this issue."));
		}
		else
		{
			FNiagaraNaniteMICOverride& NewOverride = MICOverrideMaterials.AddDefaulted_GetRef();
			NewOverride.OriginalMaterial = Materials[i];
			NewOverride.ReplacementMaterial = MICMaterials[i];
		}
	}
#endif
}

void UNiagaraNaniteRendererProperties::ApplyMaterialOverrides(const FNiagaraEmitterInstance* EmitterInstance, TArray<UMaterialInterface*>& InOutMaterials) const
{
	if (bOverrideMaterials)
	{
		const int32 NumOverrideMaterials = FMath::Min(OverrideMaterials.Num(), InOutMaterials.Num());
		for (int32 OverrideIndex = 0; OverrideIndex < NumOverrideMaterials; ++OverrideIndex)
		{
			if (!InOutMaterials[OverrideIndex])
			{
				continue;
			}

			UMaterialInterface* OverrideMat = nullptr;

			// UserParamBinding, if mapped to a real value, always wins. Otherwise, use the ExplictMat if it is set. Finally, fall
			// back to the particle mesh material. This allows the user to effectively optionally bind to a Material binding
			// and still have good defaults if it isn't set to anything.
			if (EmitterInstance && OverrideMaterials[OverrideIndex].UserParamBinding.Parameter.IsValid())
			{
				OverrideMat = Cast<UMaterialInterface>(EmitterInstance->FindBinding(OverrideMaterials[OverrideIndex].UserParamBinding.Parameter));
			}

			if (!OverrideMat)
			{
				OverrideMat = OverrideMaterials[OverrideIndex].ExplicitMat;
			}

			if (OverrideMat)
			{
				InOutMaterials[OverrideIndex] = OverrideMat;
			}
		}
	}

	// Apply MIC override materials
	if (MICOverrideMaterials.Num() > 0)
	{
		for (UMaterialInterface*& Material : InOutMaterials)
		{
			if (const FNiagaraNaniteMICOverride* Override = MICOverrideMaterials.FindByPredicate([&Material](const FNiagaraNaniteMICOverride& MICOverride) { return MICOverride.OriginalMaterial == Material; }))
			{
				Material = Override->ReplacementMaterial;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
