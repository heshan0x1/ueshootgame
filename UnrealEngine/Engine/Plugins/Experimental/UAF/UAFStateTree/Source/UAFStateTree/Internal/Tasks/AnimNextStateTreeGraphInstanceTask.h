// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextStateTreeTypes.h"
#include "Graph/AnimNextAnimationGraph.h"
#include <limits>
#include "AlphaBlend.h"
#include "Factory/AnimNextFactoryParams.h"
#include "StructUtils/PropertyBag.h"

#include "AnimNextStateTreeGraphInstanceTask.generated.h"

struct FAnimNextStateTreeTraitContext;

USTRUCT()
struct UAFSTATETREE_API FAnimNextGraphInstanceTaskInstanceData
{
	GENERATED_BODY()

	// This needs to be a set of 'active' instanced structs/property bags.
	// Available structs/property bags are populated based around asset key (query factory in editor)
	// Checkbox per 'available' struct in the UI only
	// Hitting the checkbox adds the instanced struct/property bag to the defaults
	// Checkbox-struct still visible if factory gets available struct removed (no data loss!)
	// Upgrade on load all structs/property bags (bags need source asset to reference)

	// The asset to instantiate
	UPROPERTY(EditAnywhere, Category = Animation, meta=(GetAllowedClasses="/Script/UAFAnimGraph.AnimNextAnimGraphSettings:GetAllowedAssetClasses"))
	TObjectPtr<UObject> Asset = nullptr;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FInstancedPropertyBag Payload_DEPRECATED;
#endif

	// Factory params for procedural graphs
	UPROPERTY(EditAnywhere, Category = Animation, meta=(FactorySourceProperty = "Asset"))
	FAnimNextFactoryParams Parameters;

	// Blend options for when the state is pushed
	UPROPERTY(EditAnywhere, Category = Animation)
	FAlphaBlendArgs BlendOptions;

	// Whether this task should continue to tick once state is entered
	UPROPERTY(EditAnywhere, Category = Animation)
	bool bContinueTicking = true;

	// How early to trigger complete. Setting this allows for a blend out while the timeline is still playing.
	UPROPERTY(EditAnywhere, Category = Animation)
	float CompleteBlendOutTime = 0.0f;

	// Current playback ratio 
	UPROPERTY(VisibleAnywhere, Category = Animation)
	float PlaybackRatio = 1.0f;

	// Current time remaining
	UPROPERTY(VisibleAnywhere, Category = Animation)
	float TimeLeft = std::numeric_limits<float>::infinity();

	// Current graph duration
	UPROPERTY(VisibleAnywhere, Category = Animation)
	float Duration = 0.0f;

	// Current looping status
	UPROPERTY(VisibleAnywhere, Category = Animation)
	bool bIsLooping = false;

#if WITH_EDITORONLY_DATA
	bool Serialize(FArchive& Ar);
	void PostSerialize(const FArchive& Ar);
#endif
};

template<>
struct TStructOpsTypeTraits<FAnimNextGraphInstanceTaskInstanceData> : public TStructOpsTypeTraitsBase2<FAnimNextGraphInstanceTaskInstanceData>
{
#if WITH_EDITORONLY_DATA
	enum
	{
		WithSerializer = true,
		WithPostSerialize = true,
	};
#endif
};


// Basic task pushing AnimationGraph onto blend stack
USTRUCT(meta = (DisplayName = "UAF Graph"))
struct UAFSTATETREE_API FAnimNextStateTreeGraphInstanceTask : public FAnimNextStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAnimNextGraphInstanceTaskInstanceData;

	FAnimNextStateTreeGraphInstanceTask();
	
	virtual bool Link(FStateTreeLinker& Linker) override;
protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
#if WITH_EDITOR
	virtual void PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) override;
	virtual void GetObjectReferences(TArray<const UObject*>& OutReferencedObjects, const FStateTreeDataView InstanceDataView) const override;
#endif
public:
	TStateTreeExternalDataHandle<FAnimNextStateTreeTraitContext> TraitContextHandle;
};
