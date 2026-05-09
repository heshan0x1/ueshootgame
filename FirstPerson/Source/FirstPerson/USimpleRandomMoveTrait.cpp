// Fill out your copyright notice in the Description page of Project Settings.

#include "USimpleRandomMoveTrait.h"
#include "FSimpleRandomMoveFragment.h"
#include "MassEntityTemplateRegistry.h"
#include "SimpleRandomMoveBubble.h"
#include "SimpleRandomMoveReplicator.h"

UUSimpleRandomMoveTrait::UUSimpleRandomMoveTrait()
{
}

void UUSimpleRandomMoveTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Add movement fragment
	BuildContext.AddFragment<FSimpleRandomMoveFragment>();

	// Set up replication by delegating to UMassReplicationTrait
	// This avoids linker issues with FMassReplicationParameters constructor
	if (!ReplicationTraitHelper)
	{
		ReplicationTraitHelper = NewObject<UMassReplicationTrait>(GetTransientPackage());
		ReplicationTraitHelper->Params.BubbleInfoClass = ASimpleRandomMoveClientBubbleInfo::StaticClass();
		ReplicationTraitHelper->Params.ReplicatorClass = USimpleRandomMoveReplicator::StaticClass();
	}
	ReplicationTraitHelper->BuildTemplate(BuildContext, World);
}