// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataChannelLayoutInfo.h"
#include "NiagaraDataChannel.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataChannelLayoutInfo)

void FNiagaraDataChannelGameDataLayout::Init(TConstArrayView<FNiagaraDataChannelVariable> Variables)
{
	VariableIndices.Reset();
	LwcConverters.Reserve(Variables.Num());
	for (const FNiagaraDataChannelVariable& Var : Variables)
	{
		//Sigh.
		//We must convert from the variable stored var in the data channels definition as we currently cannot serialize/store actual LWC types in FNiagaraTypeDefinitions.
		FNiagaraTypeDefinition LWCType = FNiagaraTypeHelper::GetLWCType(Var.GetType());
		FNiagaraVariableBase LWCVar(LWCType, Var.GetName());

		int32& VarIdx = VariableIndices.Add(LWCVar);
		VarIdx = VariableIndices.Num() - 1;

		FNiagaraLwcStructConverter& Converter = LwcConverters.AddDefaulted_GetRef();
		Converter = FNiagaraTypeRegistry::GetStructConverter(LWCType);
	}
}

//////////////////////////////////////////////////////////////////////////

FNiagaraDataChannelLayoutInfo::FNiagaraDataChannelLayoutInfo(const UNiagaraDataChannel* DataChannel)
{
	check(DataChannel);
	GameDataLayout.Init(DataChannel->GetVariables());

	bKeepPreviousFrameData = DataChannel->KeepPreviousFrameData();

	CompiledData.SimTarget = ENiagaraSimTarget::CPUSim;
	CompiledDataGPU.SimTarget = ENiagaraSimTarget::GPUComputeSim;
	for (const FNiagaraDataChannelVariable& NDCVar : DataChannel->GetVariables())
	{
		FNiagaraVariableBase Var = NDCVar;
		if (Var.GetType().IsEnum() == false)
		{
			Var.SetType(FNiagaraTypeDefinition(FNiagaraTypeHelper::FindNiagaraFriendlyTopLevelStruct(Var.GetType().GetScriptStruct(), ENiagaraStructConversion::Simulation)));
		}
		CompiledData.Variables.Add(Var);
		CompiledDataGPU.Variables.Add(Var);
	}
	CompiledData.BuildLayout();
	CompiledDataGPU.BuildLayout();
}

FNiagaraDataChannelLayoutInfo::~FNiagaraDataChannelLayoutInfo()
{
	CompiledData.Empty();
	CompiledDataGPU.Empty();
}
