// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraitCore/TraitBinding.h"
#include "TraitCore/TraitSharedData.h"

#include "MirroringTraitData.generated.h"

class UMirrorDataTable;

/**
 * Mirroring Setup Parameters
 *
 * This struct holds the minimal configuration needed to enable a mirror pass.
 *
 * @see UMirrorDataTable, FMirroringTrait, FMirroringAdditiveTrait
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Mirroring Trait Setup Parameters"))
struct FUAFMirroringTraitSetupParams
{
	GENERATED_BODY()

	// Whether to perform mirror pass
	UPROPERTY(EditAnywhere, Category = Settings, meta=(DisplayName = "Mirror"))
	bool bShouldMirror = true;

	// Data table to map bones to their mirrored counterpart
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ExportAsReference="true"))
	TObjectPtr<const UMirrorDataTable> MirrorDataTable = nullptr;
};

/**
 * Mirroring Apply/Filter Parameters
 *
 * This struct holds flags for the channels that can be affected during the mirror pass (i.e. Bones, Curves, Attributes).
 *
 * @see FMirroringTrait, FMirroringAdditiveTrait, FAnimNextEvaluationMirroringTask
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Mirroring Trait Apply To Parameters"))
struct FUAFMirroringTraitApplyToParams
{
	GENERATED_BODY()

	// Whether to mirror bone transforms
	UPROPERTY(EditAnywhere, Category = Channels, meta=(DisplayName = "Bones"))
	bool bShouldMirrorBones = true;

	// Whether to mirror animation curves
	UPROPERTY(EditAnywhere, Category = Channels, meta=(DisplayName = "Curves"))
	bool bShouldMirrorCurves = true;

	// Whether to mirror attributes
	UPROPERTY(EditAnywhere, Category = Channels, meta=(DisplayName = "Attributes"))
	bool bShouldMirrorAttributes = true;
};

/** A trait that can mirror an input's keyframe data. */
USTRUCT(meta = (DisplayName = "Mirroring", ShowTooltip = true))
struct FMirroringTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	// Input node to query the keyframe to be mirrored.
	UPROPERTY()
	FAnimNextTraitHandle Input;

	// Defines whether to perform mirror pass (and what data table to use).
	UPROPERTY(EditAnywhere, Category = "Setup", meta=(ShowOnlyInnerProperties))
	FUAFMirroringTraitSetupParams Setup;

	// Defines what channels will be affected during the mirror pass.
	UPROPERTY(EditAnywhere, Category = "Apply To", meta=(ShowOnlyInnerProperties))
	FUAFMirroringTraitApplyToParams ApplyTo;
	
	// Latent pin support boilerplate
	#define TRAIT_LATENT_PROPERTIES_ENUMERATOR(GeneratorMacro) \
	GeneratorMacro(Input) \
	GeneratorMacro(Setup) \
	GeneratorMacro(ApplyTo) \

	GENERATE_TRAIT_LATENT_PROPERTIES(FMirroringTraitSharedData, TRAIT_LATENT_PROPERTIES_ENUMERATOR)
	#undef TRAIT_LATENT_PROPERTIES_ENUMERATOR
};

/** Same behaviour as FMirroringTrait, but as an additive (i.e. it only mirrors the super-trait’s output). */
USTRUCT(meta = (DisplayName = "Mirroring", ShowTooltip = true))
struct FMirroringAdditiveTraitSharedData : public FAnimNextTraitSharedData
{
	GENERATED_BODY()

	// Defines whether to perform mirror pass (and what data table to use).
	UPROPERTY(EditAnywhere, Category = "Setup", meta=(ShowOnlyInnerProperties))
	FUAFMirroringTraitSetupParams Setup;

	// Defines what channels will be affected during the mirror pass.
	UPROPERTY(EditAnywhere, Category = "Apply To", meta=(ShowOnlyInnerProperties))
	FUAFMirroringTraitApplyToParams ApplyTo;
	
	// Latent pin support boilerplate
	#define TRAIT_LATENT_PROPERTIES_ENUMERATOR(GeneratorMacro) \
	GeneratorMacro(Setup) \
	GeneratorMacro(ApplyTo) \

	GENERATE_TRAIT_LATENT_PROPERTIES(FMirroringAdditiveTraitSharedData, TRAIT_LATENT_PROPERTIES_ENUMERATOR)
	#undef TRAIT_LATENT_PROPERTIES_ENUMERATOR
};

namespace UE::UAF
{
	// Namespaced alias
	using FMirroringTraitSetupParams = FUAFMirroringTraitSetupParams;
	using FMirroringTraitApplyToParams = FUAFMirroringTraitApplyToParams;
	using FMirroringTraitData = FMirroringTraitSharedData;
	using FMirroringAdditiveTraitData = FMirroringAdditiveTraitSharedData;
	
}