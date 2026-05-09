// Fill out your copyright notice in the Description page of Project Settings.

#include "SimpleRandomMoveSubsystem.h"
#include "MassReplicationSubsystem.h"
#include "SimpleRandomMoveBubble.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplate.h"

void USimpleRandomMoveSubsystem::PostInitialize()
{
	Super::PostInitialize();

	UMassReplicationSubsystem* ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(GetWorld());
	if (ReplicationSubsystem)
	{
		ReplicationSubsystem->RegisterBubbleInfoClass(ASimpleRandomMoveClientBubbleInfo::StaticClass());
	}
}

void USimpleRandomMoveSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// On clients, ensure entity template is registered so replication can spawn entities.
	// The MassSpawner normally does this in PostRegisterAllComponents, but with WorldPartition
	// the spawner might not be loaded on clients. We force-register the template here.
	if (InWorld.GetNetMode() == NM_Client)
	{
		// Load the MassEntityConfig asset used by this project
		UMassEntityConfigAsset* ConfigAsset = LoadObject<UMassEntityConfigAsset>(
			nullptr, TEXT("/Game/FirstMassConf.FirstMassConf"));
		
		if (ConfigAsset)
		{
			const FMassEntityTemplate& Template = ConfigAsset->GetOrCreateEntityTemplate(InWorld);
			UE_LOG(LogTemp, Log, TEXT("SimpleRandomMoveSubsystem: Template registered on client. TemplateID valid=%d"),
				Template.GetTemplateID().IsValid());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SimpleRandomMoveSubsystem: Failed to load FirstMassConf asset!"));
		}
	}
}