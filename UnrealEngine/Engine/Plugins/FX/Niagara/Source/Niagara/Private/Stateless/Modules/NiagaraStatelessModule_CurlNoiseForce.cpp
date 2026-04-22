// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraStatelessModule_CurlNoiseForce)

void UNiagaraStatelessModule_CurlNoiseForce::BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
{
	if (!IsModuleEnabled())
	{
		return;
	}

	NiagaraStateless::FPhysicsBuildData& PhysicsBuildData = BuildContext.GetTransientBuildData<NiagaraStateless::FPhysicsBuildData>();
	PhysicsBuildData.bNoiseEnabled = true;
	PhysicsBuildData.NoiseStrength = NoiseStrength;
	PhysicsBuildData.NoiseFrequency = NoiseFrequency;
}
