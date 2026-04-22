// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FPrimitiveSceneProxy;

/** Interface for helper object that creates and adds primitives to the scene. */
class IPCGRuntimePrimitiveFactory
{
public:
	virtual ~IPCGRuntimePrimitiveFactory() = default;

	virtual int32 GetNumPrimitives() const = 0;
	virtual FPrimitiveSceneProxy* GetSceneProxy(int32 InPrimitiveIndex) const = 0;
	virtual int32 GetNumInstances(int32 InPrimitiveIndex) const = 0;
	virtual bool IsRenderStateCreated() const = 0;
};
