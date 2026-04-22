// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PostProcess/PostProcessCompositePrimitivesCommon.h"

class FVirtualShadowMapArray;

#if UE_ENABLE_DEBUG_DRAWING
FScreenPassTexture AddDebugPrimitivePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, int32 ViewIndex, FSceneUniformBuffer& SceneUniformBuffer, FVirtualShadowMapArray* VirtualShadowMapArray, const FCompositePrimitiveInputs& Inputs);
#endif

bool IsDebugPrimitivePassEnabled(const FViewInfo& View);
