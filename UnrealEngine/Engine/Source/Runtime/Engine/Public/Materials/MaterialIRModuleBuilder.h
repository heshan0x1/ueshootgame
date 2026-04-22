// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/EngineTypes.h"
#include "Materials/MaterialIRCommon.h"

#if WITH_EDITOR

//
struct FMaterialIRModuleBuilder
{
	UMaterial* Material;
	EShaderPlatform ShaderPlatform;
	const ITargetPlatform* TargetPlatform;
	ERHIFeatureLevel::Type FeatureLevel;
	EMaterialQualityLevel::Type QualityLevel;
	EBlendMode BlendMode;
	const FStaticParameterSet& StaticParameters;
	FMaterialInsights* TargetInsights{};
	UMaterialExpression* PreviewExpression;

	bool Build(FMaterialIRModule* TargetModule);
};

#endif
