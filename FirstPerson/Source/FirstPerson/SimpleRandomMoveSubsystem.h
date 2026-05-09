// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimpleRandomMoveSubsystem.generated.h"

/**
 * World subsystem that registers the SimpleRandomMove BubbleInfo class 
 * with the MassReplication system on startup, and ensures entity templates
 * are registered on clients for replication.
 */
UCLASS()
class FIRSTPERSON_API USimpleRandomMoveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void PostInitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
