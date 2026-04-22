// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "StateTreeEditorSchema.generated.h"

#define UE_API STATETREEEDITORMODULE_API

class UStateTree;
namespace UE::StateTree::Compiler
{
	struct FPostInternalContext;
}

/**
 * Schema describing how a StateTree is edited.
 */
UCLASS(MinimalAPI)
class UStateTreeEditorSchema : public UObject
{
	GENERATED_BODY()

public:
	/** @returns True if modifying extensions in the editor is allowed. An extension can be added by code. */
	virtual bool AllowExtensions() const
	{
		return true;
	}

public:
	/*
	 * Validates and applies the schema restrictions on the StateTree.
	 * Also serves as the "safety net" of fixing up editor data following an editor operation.
	 */
	UE_API virtual void Validate(TNotNull<UStateTree*> StateTree);


	/**
	 * Handle compilation for the owning state tree asset.
	 * The state tree asset compiled successfully.
	 */
	virtual bool HandlePostInternalCompile(const UE::StateTree::Compiler::FPostInternalContext& Context)
	{
		return true;
	}
};

#undef UE_API
