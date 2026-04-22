// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessExpression.h"
#include "Stateless/NiagaraStatelessParticleSimExecData.h"

#include "NiagaraDataSetCompiledData.h"
#include "NiagaraParameterBinding.h"
#include "NiagaraParameterStore.h"
#include "NiagaraParameterStore.h"

namespace NiagaraStatelessEmitterDataBuildContextPrivate
{
	uint32 AddStaticData(TArray<uint8>& Buffer, const void* Data, uint32 DataSize)
	{
		if (DataSize == 0)
		{
			return 0;
		}

		constexpr uint32 BlockSize = 4;
		check((DataSize % BlockSize) == 0);

		constexpr bool bDeduplicateData = true;
		if (bDeduplicateData)
		{
			const uint32 iEndOffset = Buffer.Num();
			for (uint32 iOffset = 0; iOffset + DataSize <= iEndOffset; iOffset += BlockSize)
			{
				if (FMemory::Memcmp(&Buffer[iOffset], Data, DataSize) == 0)
				{
					return iOffset / BlockSize;
				}
			}
		}

		const uint32 OutIndex = Buffer.AddUninitialized(DataSize);
		FMemory::Memcpy(&Buffer[OutIndex], Data, DataSize);
		return OutIndex / BlockSize;
	}
};

void FNiagaraStatelessEmitterDataBuildContext::PreModuleBuild(int32 InShaderParameterOffset)
{
	ModuleBuiltDataOffset = BuiltData.Num();
	ShaderParameterOffset = InShaderParameterOffset;
	++RandomSeedOffest;
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<uint32> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<int32> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<float> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<FVector2f> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<FVector3f> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<FVector4f> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

uint32 FNiagaraStatelessEmitterDataBuildContext::AddStaticData(TConstArrayView<FLinearColor> StaticData) const
{
	return NiagaraStatelessEmitterDataBuildContextPrivate::AddStaticData(StaticDataBuffer, StaticData.GetData(), StaticData.GetTypeSize() * StaticData.Num());
}

int32 FNiagaraStatelessEmitterDataBuildContext::AddRendererBinding(const FNiagaraVariableBase& Variable) const
{
	int32 DataOffset = INDEX_NONE;
	if (Variable.IsValid())
	{
		FNiagaraVariable Var(Variable);
		RendererBindings.AddParameter(Var, false, false, &DataOffset);
		DataOffset /= sizeof(uint32);
	}

	return DataOffset;
}

int32 FNiagaraStatelessEmitterDataBuildContext::AddRendererBinding(const FNiagaraParameterBinding& Binding) const
{
	int32 DataOffset = INDEX_NONE;
	if (Binding.ResolvedParameter.IsValid())
	{
		FNiagaraVariable Var(Binding.ResolvedParameter);
		RendererBindings.AddParameter(Var, false, false, &DataOffset);
		DataOffset /= sizeof(uint32);
	}

	return DataOffset;
}

int32 FNiagaraStatelessEmitterDataBuildContext::AddRendererBinding(const FNiagaraParameterBindingWithValue& Binding) const
{
	int32 DataOffset = INDEX_NONE;
	if (Binding.ResolvedParameter.IsValid())
	{
		FNiagaraVariable Var(Binding.ResolvedParameter);
		RendererBindings.AddParameter(Var, false, false, &DataOffset);

		TConstArrayView<uint8> DefaultValue = Binding.GetDefaultValueArray();
		if (DefaultValue.Num() > 0)
		{
			check(DataOffset != INDEX_NONE);
			check(DefaultValue.Num() == Var.GetSizeInBytes());
			RendererBindings.SetParameterData(DefaultValue.GetData(), DataOffset, DefaultValue.Num());
		}

		DataOffset /= sizeof(uint32);
	}

	return DataOffset;
}


int32 FNiagaraStatelessEmitterDataBuildContext::AddExpression(const FInstancedStruct& ExpressionStruct) const
{
	int32 DataOffset = INDEX_NONE;
	if (const FNiagaraStatelessExpression* Expression = ExpressionStruct.GetPtr<FNiagaraStatelessExpression>())
	{
		const FName ExpressionName("__StatelessInternal__.Expression", Expressions.Num());
		const FNiagaraVariable ExpressionVariable(Expression->GetOutputTypeDef(), ExpressionName);
		RendererBindings.AddParameter(ExpressionVariable, false, false, &DataOffset);

		Expressions.Emplace(DataOffset, Expression->Build(*this));
		DataOffset /= sizeof(uint32);
	}
	return DataOffset;
}

void FNiagaraStatelessEmitterDataBuildContext::AddParticleSimulationExecSimulate(TFunction<void(const NiagaraStateless::FParticleSimulationContext&)> Func) const
{
	if (!ParticleExecData)
	{
		return;
	}

	ParticleExecData->SimulateFunctions.Emplace(MoveTemp(Func), ModuleBuiltDataOffset, ShaderParameterOffset, RandomSeedOffest);
}

int32 FNiagaraStatelessEmitterDataBuildContext::FindParticleVariableIndex(const FNiagaraVariableBase& Variable) const
{
	return ParticleDataSet.Variables.IndexOfByKey(Variable);
}
