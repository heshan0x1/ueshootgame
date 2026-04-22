// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/Modules/NiagaraStatelessModule_CameraOffset.h"

#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraStatelessModule_CameraOffset)

namespace NSMCameraOffsetPrivate
{
	struct FModuleBuiltData
	{
		FUintVector3	DistributionParameters = FUintVector3::ZeroValue;
		int32			CameraVariableOffset = INDEX_NONE;
		int32			PreviousCameraVariableOffset = INDEX_NONE;
	};

	using FParameters = NiagaraStateless::FCameraOffsetModule_ShaderParameters;

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const float* NormalizedAgeData = ParticleSimulationContext.GetParticleNormalizedAge();
		const float* PreviousNormalizedAgeData = ParticleSimulationContext.GetParticlePreviousNormalizedAge();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			float CameraOffsets[2];
			ParticleSimulationContext.SampleDistributionValues(ModuleBuiltData->DistributionParameters, i, 0, NormalizedAgeData[i], PreviousNormalizedAgeData[i], CameraOffsets[0], CameraOffsets[1]);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->CameraVariableOffset, i, CameraOffsets[0]);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousCameraVariableOffset, i, CameraOffsets[1]);
		}
	}
}

void UNiagaraStatelessModule_CameraOffset::BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
{
	using namespace NSMCameraOffsetPrivate;

	const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();

	FModuleBuiltData* BuiltData				= BuildContext.AllocateBuiltData<FModuleBuiltData>();
	BuiltData->CameraVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.CameraOffsetVariable);
	BuiltData->PreviousCameraVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousCameraOffsetVariable);

	const bool bAttributesUsed = (BuiltData->CameraVariableOffset != INDEX_NONE || BuiltData->PreviousCameraVariableOffset != INDEX_NONE);
	if (IsModuleEnabled() && bAttributesUsed)
	{
		BuiltData->DistributionParameters = BuildContext.AddDistribution(CameraOffsetDistribution);

		BuildContext.AddParticleSimulationExecSimulate(&ParticleSimulate);
	}
}

void UNiagaraStatelessModule_CameraOffset::BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const
{
	using namespace NSMCameraOffsetPrivate;

	ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
}

void UNiagaraStatelessModule_CameraOffset::SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const
{
	using namespace NSMCameraOffsetPrivate;

	const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

	FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
	Parameters->CameraOffset_Distribution = ModuleBuiltData->DistributionParameters;
}

#if WITH_EDITORONLY_DATA
const TCHAR* UNiagaraStatelessModule_CameraOffset::GetShaderTemplatePath() const
{
	return TEXT("/Plugin/FX/Niagara/Private/Stateless/Modules/NiagaraStatelessModule_CameraOffset.ush");
}

void UNiagaraStatelessModule_CameraOffset::GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables, EVariableFilter Filter) const
{
	const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
	OutVariables.AddUnique(StatelessGlobals.CameraOffsetVariable);
	OutVariables.AddUnique(StatelessGlobals.PreviousCameraOffsetVariable);
}
#endif
