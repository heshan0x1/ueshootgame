// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "StateTreeEditorDataExtension.generated.h"

namespace UE::StateTree::Compiler
{
	struct FPostInternalContext;
}

class IDetailLayoutBuilder;
class UStateTreeEditorData;
class UStateTreeState;

/**
 * Extension for the editor data of the state tree asset.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, Within=StateTreeEditorData, MinimalAPI)
class UStateTreeEditorDataExtension : public UObject
{
	GENERATED_BODY()

public:
	virtual bool HandlePostInternalCompile(const UE::StateTree::Compiler::FPostInternalContext& Context)
	{
		return true;
	}

	virtual void CustomizeDetails(TNonNullPtr<UStateTreeState> State, IDetailLayoutBuilder& DetailBuilder)
	{
	}

protected:
	UStateTreeEditorData* GetStateTreeEditorData() const
	{
		return GetOuterUStateTreeEditorData();
	}
};
