// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GPUSceneWriter.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

#define UE_API NIAGARANANITESHADER_API

class FNiagaraNaniteGPUSceneCS : public FGlobalShader
{
	DECLARE_EXPORTED_GLOBAL_SHADER(FNiagaraNaniteGPUSceneCS, UE_API);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraNaniteGPUSceneCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSize	= 64;
	static constexpr int32 MaxCustomFloat4s	= 16;
	static constexpr int32 MaxCustomFloats	= MaxCustomFloat4s * 4;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, UE_API)
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneWriterParameters, GPUSceneWriterParameters)

		SHADER_PARAMETER(uint32,			NumAllocatedInstances)
		SHADER_PARAMETER(uint32,			ParticleCpuCount)
		SHADER_PARAMETER(uint32,			ParticleGpuCountOffset)

		SHADER_PARAMETER(uint32,			ParticleBufferStride)
		SHADER_PARAMETER_SRV(Buffer<float>,	ParticleFloatData)
		SHADER_PARAMETER_SRV(Buffer<float>, ParticleHalfData)
		SHADER_PARAMETER_SRV(Buffer<int>,	ParticleIntData)
		SHADER_PARAMETER_SRV(Buffer<uint>,	ParticleCountBuffer)

		SHADER_PARAMETER(uint32,				NumCustomFloats)
		SHADER_PARAMETER(uint32,				NumCustomFloat4s)
		SHADER_PARAMETER_ARRAY(FUintVector4,	CustomFloatComponents,	[MaxCustomFloat4s])
		SHADER_PARAMETER_ARRAY(FVector4f,		DefaultCustomFloats,	[MaxCustomFloat4s])

		SHADER_PARAMETER(uint32,			PositionComponentOffset)
		SHADER_PARAMETER(uint32,			RotationComponentOffset)
		SHADER_PARAMETER(uint32,			ScaleComponentOffset)

		SHADER_PARAMETER(uint32,			PrevPositionComponentOffset)
		SHADER_PARAMETER(uint32,			PrevRotationComponentOffset)
		SHADER_PARAMETER(uint32,			PrevScaleComponentOffset)

		SHADER_PARAMETER(FVector3f,			DefaultPosition)
		SHADER_PARAMETER(FQuat4f,			DefaultRotation)
		SHADER_PARAMETER(FVector3f,			DefaultScale)

		SHADER_PARAMETER(FVector3f,			DefaultPrevPosition)
		SHADER_PARAMETER(FQuat4f,			DefaultPrevRotation)
		SHADER_PARAMETER(FVector3f,			DefaultPrevScale)

		SHADER_PARAMETER(FVector3f,			MeshScale)
		SHADER_PARAMETER(FQuat4f,			MeshRotation)

		SHADER_PARAMETER(int,				MeshIndex)
		SHADER_PARAMETER(int,				RendererVis)
		SHADER_PARAMETER(uint32,			MeshIndexComponentOffset)
		SHADER_PARAMETER(uint32,			RendererVisComponentOffset)

		SHADER_PARAMETER(FVector3f,			SimulationToComponent_Translation)
		SHADER_PARAMETER(FQuat4f,			SimulationToComponent_Rotation)
		SHADER_PARAMETER(FVector3f,			SimulationToComponent_Scale)
		SHADER_PARAMETER(FVector3f,			PreviousSimulationToComponent_Translation)
		SHADER_PARAMETER(FQuat4f,			PreviousSimulationToComponent_Rotation)
		SHADER_PARAMETER(FVector3f,			PreviousSimulationToComponent_Scale)

		SHADER_PARAMETER(FVector3f,			SimulationLWCTile)

		SHADER_PARAMETER(uint32,			PrimitiveId)
	END_SHADER_PARAMETER_STRUCT()
};

#undef UE_API
