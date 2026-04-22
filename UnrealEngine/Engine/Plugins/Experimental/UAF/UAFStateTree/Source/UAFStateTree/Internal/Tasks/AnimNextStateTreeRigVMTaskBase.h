// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextFunctionHandle.h"
#include "AnimNextStateTreeTypes.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "StateTreeTaskBase.h"

#include "AnimNextStateTreeRigVMTaskBase.generated.h"

class UAnimNextDataInterface;
class UAnimNextDataInterface_EditorData;
class URigVMGraph;
class UStateTreeState;

struct FAnimNextStateTreeProgrammaticFunctionHeaderParams;
struct FStateTreeBindableStructDesc;
struct FStateTreeExecutionContext;
struct FRigVMClient;
struct FAnimNextStateTreeTraitContext;

USTRUCT()
struct UAFSTATETREE_API FAnimNextStateTreeRigVMTaskInstanceData
{
	GENERATED_BODY()

	/** Parameters to use to call the function */
	UPROPERTY(EditAnywhere, Category = "Parameters", Meta = (FixedLayout, ShowOnlyInnerProperties))
	FInstancedPropertyBag Parameters;

	/** Cached function handle */
	UE::UAF::FFunctionHandle FunctionHandle;
};

/**
 * Wrapper for RigVM based Tasks. 
 */
USTRUCT()
struct UAFSTATETREE_API FAnimNextStateTreeRigVMTaskBase : public FAnimNextStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAnimNextStateTreeRigVMTaskInstanceData;

public:

	//~ Begin FStateTreeTaskBase Interface
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	//~ End FStateTreeTaskBase Interface

	//~ Begin FStateTreeNodeBase Interface
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual void PostEditNodeChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) override;
#endif
	//~ End FStateTreeNodeBase Interface
	
#if WITH_EDITOR
	/** Adds Task function headers to parent StateTree RigVM */
	virtual void GetProgrammaticFunctionHeaders(FAnimNextStateTreeProgrammaticFunctionHeaderParams& InProgrammaticFunctionHeaderParams, const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc);
#endif

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Parameter", Meta = (GetOptions = "UAFStateTreeUncookedOnly.AnimNextStateTreeFunctionLibraryHelper.GetExposedAnimNextFunctionNames"))
	FName TaskFunctionName = NAME_None;
#endif

	UPROPERTY(EditAnywhere, Category = "Parameters")
	FRigVMGraphFunctionHeader RigVMFunctionHeader = FRigVMGraphFunctionHeader();

	/** Owning state name, populated during programmatic graph creation */
	UPROPERTY()
	FName StateName = NAME_None;

	/** External node ID defined by owning state tree, populated during programmatic graph creation */
	UPROPERTY()
	FGuid NodeId = FGuid();

	UPROPERTY()
	FName InternalEventName = NAME_None;

	UPROPERTY()
	int32 ResultIndex = INDEX_NONE;

public:
	TStateTreeExternalDataHandle<FAnimNextStateTreeTraitContext> TraitContextHandle;
};