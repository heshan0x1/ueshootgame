// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessCommon.h"

class UNiagaraStatelessEmitterTemplate;

namespace FNiagaraStatelessEmitterTranslator
{
	void TranslateToCompute(FString& HlslOutput, UNiagaraStatelessEmitterTemplate* EmitterTemplate);
}
