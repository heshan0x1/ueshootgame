// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextFunctionHandle.h"
#include "Variables/AnimNextSharedVariables.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "StateTreeConditionBase.h"
#include "StateTreeReference.h"

#include "AnimNextStateTreeRigVMConditionBase.generated.h"

class UAnimNextSharedVariables;
class UAnimNextSharedVariables_EditorData;
class URigVMGraph;
class UStateTreeState;

struct FAnimNextStateTreeProgrammaticFunctionHeaderParams;
struct FStateTreeBindableStructDesc;
struct FStateTreeExecutionContext;
struct FAnimNextStateTreeTraitContext;
struct FRigVMClient;

USTRUCT()
struct FAnimNextStateTreeRigVMConditionInstanceData
{
	GENERATED_BODY()

	/** Parameters to use to call the function */
	UPROPERTY(EditAnywhere, Category = "Parameters", Meta = (FixedLayout, ShowOnlyInnerProperties))
	FInstancedPropertyBag Parameters;

	/** Cached function handle */
	UE::UAF::FFunctionHandle FunctionHandle;
};

/**
 * Wrapper for RigVM based Conditions. 
 */
USTRUCT()
struct FAnimNextStateTreeRigVMConditionBase : public FStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FAnimNextStateTreeRigVMConditionInstanceData;

public:

	//~ Begin FStateTreeConditionBase Interface
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	//~ End FStateTreeConditionBase Interface

	//~ Begin FStateTreeNodeBase Interface
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual void PostEditNodeChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView) override;
#endif
	//~ End FStateTreeNodeBase Interface
	
#if WITH_EDITOR
	/** Adds condition function headers to parent StateTree RigVM */
	virtual void GetProgrammaticFunctionHeaders(FAnimNextStateTreeProgrammaticFunctionHeaderParams& InProgrammaticFunctionHeaderParams, const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc);
#endif

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Parameter", Meta = (GetOptions = "UAFStateTreeUncookedOnly.AnimNextStateTreeFunctionLibraryHelper.GetExposedAnimNextFunctionNames"))
	FName ConditionFunctionName = NAME_None;
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