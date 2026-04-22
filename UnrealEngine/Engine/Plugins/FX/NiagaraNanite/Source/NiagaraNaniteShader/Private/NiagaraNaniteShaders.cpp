// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNaniteShaders.h"

void FNiagaraNaniteGPUSceneCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	OutEnvironment.SetDefine(TEXT("THREAD_GROUP_SIZE"), ThreadGroupSize);
	OutEnvironment.SetDefine(TEXT("MAX_CUSTOM_FLOAT4S"), MaxCustomFloat4s);
}

IMPLEMENT_GLOBAL_SHADER(FNiagaraNaniteGPUSceneCS, "/Plugin/FX/NiagaraNanite/NiagaraNaniteGPUScene.usf", "UpdateMeshInstancesCS", SF_Compute);
