// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/TedsActorMobilityQueries.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Columns/TedsActorMobilityColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TedsActorMobilityQueries)

void UActorMobilityDataStorageFactory::RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	RegisterActorAddMobilityColumn(DataStorage);
	RegisterActorMobilityToColumnQuery(DataStorage);
	RegisterMobilityColumnToActorQuery(DataStorage);
}

void UActorMobilityDataStorageFactory::RegisterActorAddMobilityColumn(UE::Editor::DataStorage::ICoreProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Add Actor Mobility to a New Column"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
			.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Object)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Object.Object); ActorInstance != nullptr)
				{
					if (const USceneComponent* ActorInstanceRootComponent = ActorInstance->GetRootComponent())
					{
						Context.AddColumn(Row, FTedsActorMobilityColumn{ .Mobility = ActorInstanceRootComponent->GetMobility() });
					}
				}
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncFromWorldTag>()
			.None<FTedsActorMobilityColumn>()
		.Compile()
	);
}

void UActorMobilityDataStorageFactory::RegisterActorMobilityToColumnQuery(UE::Editor::DataStorage::ICoreProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync Actor's Mobility to Column"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
			.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Object, FTedsActorMobilityColumn& MobilityColumn)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Object.Object); ActorInstance != nullptr)
				{
					if (const USceneComponent* ActorInstanceRootComponent = ActorInstance->GetRootComponent())
					{
						MobilityColumn.Mobility = ActorInstanceRootComponent->GetMobility();
					}
				}
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncFromWorldTag>()
		.Compile()
	);
}

void UActorMobilityDataStorageFactory::RegisterMobilityColumnToActorQuery(UE::Editor::DataStorage::ICoreProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync Column to Actor's Mobility"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal)).SetExecutionMode(EExecutionMode::GameThread),
			[&](IQueryContext& Context, RowHandle Row, FTypedElementUObjectColumn& Object, const FTedsActorMobilityColumn& MobilityColumn)
			{
				if (AActor* ActorInstance = Cast<AActor>(Object.Object); ActorInstance != nullptr)
				{
						if (USceneComponent* ActorInstanceRootComponent = ActorInstance->GetRootComponent())
						{
							ActorInstanceRootComponent->SetMobility(MobilityColumn.Mobility);
						}
				}
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncBackToWorldTag>()
		.Compile()
	);
}
