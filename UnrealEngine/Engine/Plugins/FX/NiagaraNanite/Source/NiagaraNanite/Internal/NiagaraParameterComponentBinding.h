// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

#include "NiagaraParameterComponentBinding.generated.h"

USTRUCT()
struct FNiagaraParameterComponentBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FNiagaraVariableBase ResolvedParameter;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FNiagaraVariableBase AliasedParameter;

	UPROPERTY()
	FNiagaraVariableBase DisplayParameter;
#endif

	UPROPERTY()
	int Component = 0;

	UPROPERTY()
	float DefaultValue = 0.0f;

#if WITH_EDITORONLY_DATA
	virtual ~FNiagaraParameterComponentBinding() = default;
	virtual FNiagaraTypeDefinition GetTypeDef() const { checkNoEntry(); return FNiagaraTypeDefinition(); }
	void OnRenameEmitter(FStringView EmitterName);
	void OnRenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, FStringView EmitterName);
	void OnRemoveVariable(const FNiagaraVariableBase& OldVariable, FStringView EmitterName);
#endif
};

USTRUCT()
struct FNiagaraFloatParameterComponentBinding : public FNiagaraParameterComponentBinding
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	virtual ~FNiagaraFloatParameterComponentBinding() = default;
	virtual FNiagaraTypeDefinition GetTypeDef() const override { return FNiagaraTypeDefinition::GetFloatDef(); }
#endif
};
