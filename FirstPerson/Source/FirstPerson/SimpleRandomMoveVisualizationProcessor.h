// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DebugVisLocationProcessor.h"
#include "AssignDebugVisProcessor.h"
#include "SimpleRandomMoveVisualizationProcessor.generated.h"

/**
 * Client-side debug visualization location processor.
 * The base UDebugVisLocationProcessor has default ExecutionFlags = Server|Standalone,
 * which means it doesn't run on Client. This subclass overrides ExecutionFlags
 * to include Client so that replicated entities can be visualized on clients.
 */
UCLASS()
class USimpleRandomMoveDebugVisProcessor : public UDebugVisLocationProcessor
{
	GENERATED_BODY()

public:
	USimpleRandomMoveDebugVisProcessor();
};

/**
 * Client-side observer that assigns ISM instance indices to newly spawned entities.
 * The base UAssignDebugVisProcessor has default ExecutionFlags = Server|Standalone,
 * so this subclass adds Client execution support.
 */
UCLASS()
class USimpleRandomMoveAssignDebugVisProcessor : public UAssignDebugVisProcessor
{
	GENERATED_BODY()

public:
	USimpleRandomMoveAssignDebugVisProcessor();
};
