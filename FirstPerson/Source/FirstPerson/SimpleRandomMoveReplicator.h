// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassReplicationProcessor.h"
#include "SimpleRandomMoveReplicator.generated.h"

/**
 * Replicator that handles replication of SimpleRandomMove entities.
 * Runs on the server, queries Mass entity fragments and sets those values
 * for replication when appropriate using the ClientBubbleHandler.
 */
UCLASS()
class FIRSTPERSON_API USimpleRandomMoveReplicator : public UMassReplicatorBase
{
	GENERATED_BODY()

public:
	/**
	 * Adds specific entity query requirements for replication.
	 */
	virtual void AddRequirements(FMassEntityQuery& EntityQuery) override;

	/**
	 * Processes the client replication.
	 * Calls CalculateClientReplication with appropriate callback implementations.
	 */
	virtual void ProcessClientReplication(FMassExecutionContext& Context, FMassReplicationContext& ReplicationContext) override;
};
