// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraParameterBinding.h"
#include "NiagaraParameterComponentBinding.h"
#include "NiagaraNaniteRendererProperties.generated.h"

#define UE_API NIAGARANANITE_API

class UMaterialInstanceConstant;
class UMaterialInterface;
class FNiagaraEmitterInstance;
class SWidget;

enum class ENiagaraNaniteVFLayout
{
	Position,
	Rotation,
	Scale,

	PrevPosition,
	PrevRotation,
	PrevScale,

	Max
};

USTRUCT()
struct FNiagaraNaniteMeshRendererMeshProperties
{
	GENERATED_BODY()

	UE_API FNiagaraNaniteMeshRendererMeshProperties();

	/** The mesh to use when rendering this slot */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	TObjectPtr<UStaticMesh> Mesh;

	/** Scale of the mesh */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FVector3f Scale = FVector3f::OneVector;

	/** Rotation of the mesh */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FRotator3f Rotation = FRotator3f::ZeroRotator;

	/** Offset of the mesh pivot */
	//UPROPERTY(EditAnywhere, Category = "Mesh")
	//FVector3f PivotOffset = FVector3f::ZeroVector;

	/** Binding to supported mesh types. */
	UPROPERTY(EditAnywhere, Category = "Mesh")
	FNiagaraParameterBinding MeshParameterBinding;

	UStaticMesh* GetResolvedMesh(const FNiagaraEmitterInstance* InEmitter) const;
};

USTRUCT()
struct FNiagaraNaniteMICOverride
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OriginalMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceConstant> ReplacementMaterial;
};

USTRUCT()
struct FNiagaraNaniteMaterialOverride
{
	GENERATED_USTRUCT_BODY()
public:
	FNiagaraNaniteMaterialOverride();

	/** Use this UMaterialInterface if set to a valid value. This will be subordinate to UserParamBinding if it is set to a valid user variable.*/
	UPROPERTY(EditAnywhere, Category = "Material", meta = (EditCondition = "bOverrideMaterials"))
	TObjectPtr<UMaterialInterface> ExplicitMat;

	/** Use the UMaterialInterface bound to this user variable if it is set to a valid value. If this is bound to a valid value and ExplicitMat is also set, UserParamBinding wins.*/
	UPROPERTY(EditAnywhere, Category = "Material", meta = (EditCondition = "bOverrideMaterials"))
	FNiagaraUserParameterBinding UserParamBinding;

	bool operator==(const FNiagaraNaniteMaterialOverride& Other)const
	{
		return UserParamBinding == Other.UserParamBinding && ExplicitMat == Other.ExplicitMat;
	}
};

UCLASS(editinlinenew, MinimalAPI, meta = (DisplayName = "Nanite Renderer"))
class UNiagaraNaniteRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNiagaraNaniteRendererProperties();

	//UObject Interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif// WITH_EDITORONLY_DATA
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//~ UNiagaraRendererProperties interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override;
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return true; };
#if WITH_EDITORONLY_DATA
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
	virtual void GetAdditionalVariables(TArray<FNiagaraVariableBase>& OutArray) const override;
	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FNiagaraRendererFeedback>& OutErrors, TArray<FNiagaraRendererFeedback>& OutWarnings, TArray<FNiagaraRendererFeedback>& OutInfo) const override;
	virtual TArray<FNiagaraVariable> GetBoundAttributes() const override;
	virtual void RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const FVersionedNiagaraEmitterBase& InEmitter) override;
	virtual void RemoveVariable(const FNiagaraVariableBase& OldVariable, const FVersionedNiagaraEmitterBase& InEmitter) override;
	virtual void RenameEmitter(const FName& InOldName, const UNiagaraEmitter* InRenamedEmitter) override;
#endif // WITH_EDITORONLY_DATA
	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) override;
	virtual void UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit = false) override;
	virtual ENiagaraRendererSourceDataMode GetCurrentSourceMode() const override { return SourceMode; }
	virtual bool PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore) override;
	virtual bool NeedsMIDsForMaterials() const override { return MaterialParameters.HasAnyBindings(); }
	virtual bool NeedsSystemPostTick() const override { return true; }
	virtual bool NeedsSystemCompletion() const override { return true; }
	//UNiagaraRendererProperties Interface END

	void GetMeshUsedMaterials(const FNiagaraNaniteMeshRendererMeshProperties& MeshProperties, const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const;

	void UpdatePreviousBindings(const FVersionedNiagaraEmitterBase& Emitter);

	void UpdateMICs();

	void ApplyMaterialOverrides(const FNiagaraEmitterInstance* EmitterInstance, TArray<UMaterialInterface*>& InOutMaterials) const;

	UPROPERTY(EditAnywhere, Category = "Mesh Rendering")
	TArray<FNiagaraNaniteMeshRendererMeshProperties> Meshes;

	/** Whether or not to draw a single element for the Emitter or to draw the particles.*/
	UPROPERTY(EditAnywhere, Category = "Mesh Rendering")
	ENiagaraRendererSourceDataMode SourceMode = ENiagaraRendererSourceDataMode::Particles;

	/** Whether or not to use the OverrideMaterials array instead of the mesh's existing materials.*/
	UPROPERTY(EditAnywhere, Category = "Mesh Rendering", DisplayName = "Enable Material Overrides")
	uint32 bOverrideMaterials : 1;

	/**
	The materials to be used instead of the StaticMesh's materials.
	Note that each material must have the Niagara Mesh Particles flag checked.
	If the ParticleMesh	requires more materials than exist in this array or any entry in this array is set to None, we will use the ParticleMesh's existing Material instead.
	*/
	UPROPERTY(EditAnywhere, Category = "Mesh Rendering", meta = (EditCondition = "bOverrideMaterials", EditConditionHides))
	TArray<FNiagaraNaniteMaterialOverride> OverrideMaterials;

	UPROPERTY()
	TArray<FNiagaraNaniteMICOverride> MICOverrideMaterials;

	/** If a render visibility tag is present, particles whose tag matches this value will be visible in this renderer. */
	UPROPERTY(EditAnywhere, Category = "Nanite Rendering")
	int32 RendererVisibility = 0;

	/** Which attribute should we use for position when generating instanced meshes?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Which attribute should we use for orienting meshes when generating instanced meshes?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RotationBinding;

	/** Which attribute should we use for scale when generating instanced meshes?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding ScaleBinding;

	/** Which attribute should we use for the renderer visibility tag? */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RendererVisibilityTagBinding;

	/** Which attribute should we use to pick the element in the mesh array on the mesh renderer? */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding MeshIndexBinding;

	/** If this array has entries, we will create a MaterialInstanceDynamic per Emitter instance from Material and set the Material parameters using the Niagara simulation variables listed. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraRendererMaterialParameters MaterialParameters;

	/** Defines data we should pass through as per instance data inside the material. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	TArray<FNiagaraFloatParameterComponentBinding> PerInstanceDataBindings;

	/** Indicies into particle dataset, can be INDEX_NONE if we are reading from none particle data. */
	TArray<int32> PerInstanceDataFloatComponents;

	// The following bindings are not provided by the user, but are filled based on what other bindings are set to, and the value of bGenerateAccurateMotionVectors
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PreviousPositionBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PreviousRotationBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PreviousScaleBinding;

	FNiagaraRendererLayout						RendererLayout;

	FNiagaraDataSetAccessor<FNiagaraPosition>	PositionDataSetAccessor;
	FNiagaraDataSetAccessor<FQuat4f>			RotationDataSetAccessor;
	FNiagaraDataSetAccessor<FVector3f>			ScaleDataSetAccessor;
	FNiagaraDataSetAccessor<int32>				RendererVisTagDataSetAccessor;
	FNiagaraDataSetAccessor<int32>				MeshIndexDataSetAccessor;

	FNiagaraDataSetAccessor<FNiagaraPosition>	PreviousPositionDataSetAccessor;
	FNiagaraDataSetAccessor<FQuat4f>			PreviousRotationDataSetAccessor;
	FNiagaraDataSetAccessor<FVector3f>			PreviousScaleDataSetAccessor;
};

#undef UE_API
