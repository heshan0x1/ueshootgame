// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassReplicationTrait.h"
#include "USimpleRandomMoveTrait.generated.h"

/**
 * Trait that configures SimpleRandomMove entities with movement and replication support.
 * Includes replication setup using ASimpleRandomMoveClientBubbleInfo and USimpleRandomMoveReplicator.
 */
UCLASS(meta=(DisplayName="Simple Random Move"))
class FIRSTPERSON_API UUSimpleRandomMoveTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	UUSimpleRandomMoveTrait();

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

private:
	/** Internal replication trait used to set up replication fragments */
	UPROPERTY(Transient)
	mutable TObjectPtr<UMassReplicationTrait> ReplicationTraitHelper = nullptr;
};