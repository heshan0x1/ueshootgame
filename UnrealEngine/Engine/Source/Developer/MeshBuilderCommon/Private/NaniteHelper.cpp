// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteHelper.h"
#include "Engine/EngineTypes.h"
#include "HAL/IConsoleManager.h"

namespace Nanite
{

void CorrectFallbackSettings(FMeshNaniteSettings& NaniteSettings, int32 NumTris, bool bIsAssembly, bool bIsRayTracing)
{
	static const auto CVarFallbackThreshold = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Nanite.Builder.FallbackTriangleThreshold"));
	static const auto CVarFallbackTargetAutoRelativeError = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Nanite.Builder.FallbackTargetAutoRelativeError"));
	static const auto CVarRayTracingProxyFallbackTargetAutoRelativeError = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Nanite.Builder.RayTracingProxy.FallbackTargetAutoRelativeError"));

	const float AutoRelativeError = bIsRayTracing ? CVarRayTracingProxyFallbackTargetAutoRelativeError->GetValueOnAnyThread() : CVarFallbackTargetAutoRelativeError->GetValueOnAnyThread();

	switch (NaniteSettings.FallbackTarget)
	{
		case ENaniteFallbackTarget::Auto:
			NaniteSettings.FallbackPercentTriangles = 1.0f;
			NaniteSettings.FallbackRelativeError = (!bIsAssembly && NumTris <= CVarFallbackThreshold->GetValueOnAnyThread()) ? 0.0f : AutoRelativeError;
			break;
		case ENaniteFallbackTarget::PercentTriangles:
			NaniteSettings.FallbackRelativeError = 0.0f;
			break;
		case ENaniteFallbackTarget::RelativeError:
			NaniteSettings.FallbackPercentTriangles = 1.0f;
			break;
	}
}

void InheritAssemblySettings(FMeshNaniteSettings& PartMeshSettings, const FMeshNaniteSettings& AssemblyMeshSettings)
{
	PartMeshSettings.bEnabled = true; // Force enable Nanite for assemblies

	// For now, inherit these from the parent settings
	// TODO: Nanite-Assemblies - These overrides have to be considered if/when we cache the part intermediates in DDC.
	PartMeshSettings.ShapePreservation		= AssemblyMeshSettings.ShapePreservation;
	PartMeshSettings.bExplicitTangents		= AssemblyMeshSettings.bExplicitTangents;
	PartMeshSettings.bLerpUVs				= AssemblyMeshSettings.bLerpUVs;
	PartMeshSettings.MaxEdgeLengthFactor	= AssemblyMeshSettings.MaxEdgeLengthFactor;
	PartMeshSettings.NumRays				= AssemblyMeshSettings.NumRays;
	PartMeshSettings.VoxelLevel				= AssemblyMeshSettings.VoxelLevel;
	PartMeshSettings.RayBackUp				= AssemblyMeshSettings.RayBackUp;
	PartMeshSettings.bSeparable				= AssemblyMeshSettings.bSeparable;
	PartMeshSettings.bVoxelNDF				= AssemblyMeshSettings.bVoxelNDF;
	PartMeshSettings.bVoxelOpacity			= AssemblyMeshSettings.bVoxelOpacity;
}

}
