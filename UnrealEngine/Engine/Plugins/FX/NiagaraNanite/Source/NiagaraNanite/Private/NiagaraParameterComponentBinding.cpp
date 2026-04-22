// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterComponentBinding.h"
#include "NiagaraConstants.h"

#if WITH_EDITORONLY_DATA
void FNiagaraParameterComponentBinding::OnRenameEmitter(FStringView EmitterName)
{
	if (AliasedParameter.IsInNameSpace(FNiagaraConstants::EmitterNamespaceString))
	{
		ResolvedParameter = AliasedParameter;
		ResolvedParameter.ReplaceRootNamespace(FNiagaraConstants::EmitterNamespaceString, EmitterName);
	}
}

void FNiagaraParameterComponentBinding::OnRenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, FStringView EmitterName)
{
	if (AliasedParameter.GetName() == OldVariable.GetName() && AliasedParameter.GetType() == OldVariable.GetType())
	{
		if (!AliasedParameter.IsInNameSpace(FNiagaraConstants::EmitterNamespaceString) || ResolvedParameter.IsInNameSpace(EmitterName))
		{
			AliasedParameter = NewVariable;
			ResolvedParameter = AliasedParameter;
			ResolvedParameter.ReplaceRootNamespace(FNiagaraConstants::EmitterNamespaceString, EmitterName);

			FNameBuilder OldDisplayName(DisplayParameter.GetName());
			if (ensure(OldDisplayName.Len() > 0))
			{
				FNameBuilder NewDisplayName(AliasedParameter.GetName());
				NewDisplayName.AppendChar('.');
				NewDisplayName.AppendChar(OldDisplayName.LastChar());
				DisplayParameter.SetName(FName(NewDisplayName));
			}
		}
	}
}

void FNiagaraParameterComponentBinding::OnRemoveVariable(const FNiagaraVariableBase& OldVariable, FStringView EmitterName)
{
	if (AliasedParameter.GetName() == OldVariable.GetName() && AliasedParameter.GetType() == OldVariable.GetType())
	{
		if (!AliasedParameter.IsInNameSpace(FNiagaraConstants::EmitterNamespaceString) || ResolvedParameter.IsInNameSpace(EmitterName) )
		{
			AliasedParameter = FNiagaraVariableBase();
			ResolvedParameter = FNiagaraVariableBase();
			DisplayParameter = FNiagaraVariableBase();
		}
	}
}
#endif
