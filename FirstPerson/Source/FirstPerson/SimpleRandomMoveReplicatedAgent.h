// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassReplicationTransformHandlers.h"
#include "MassReplicationTypes.h"
#include "MassClientBubbleHandler.h"
#include "SimpleRandomMoveReplicatedAgent.generated.h"

/**
 * The data that is replicated specific to each SimpleRandomMove agent.
 * We replicate position and yaw using the built-in FReplicatedAgentPositionYawData.
 */
USTRUCT()
struct FReplicatedSimpleRandomMoveAgent : public FReplicatedAgentBase
{
	GENERATED_BODY()

	const FReplicatedAgentPositionYawData& GetReplicatedPositionYawData() const { return PositionYaw; }

	/** This function is required to be provided in FReplicatedAgentBase derived classes that use FReplicatedAgentPositionYawData */
	FReplicatedAgentPositionYawData& GetReplicatedPositionYawDataMutable() { return PositionYaw; }

private:
	UPROPERTY(Transient)
	FReplicatedAgentPositionYawData PositionYaw;
};

/** Fast array item for efficient agent replication */
USTRUCT()
struct FSimpleRandomMoveFastArrayItem : public FMassFastArrayItemBase
{
	GENERATED_BODY()

	FSimpleRandomMoveFastArrayItem() = default;
	FSimpleRandomMoveFastArrayItem(const FReplicatedSimpleRandomMoveAgent& InAgent, const FMassReplicatedAgentHandle InHandle)
		: FMassFastArrayItemBase(InHandle)
		, Agent(InAgent)
	{}

	/** This typedef is required to be provided in FMassFastArrayItemBase derived classes */
	typedef FReplicatedSimpleRandomMoveAgent FReplicatedAgentType;

	UPROPERTY()
	FReplicatedSimpleRandomMoveAgent Agent;
};
