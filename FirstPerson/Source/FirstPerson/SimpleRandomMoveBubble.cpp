// Fill out your copyright notice in the Description page of Project Settings.

#include "SimpleRandomMoveBubble.h"
#include "MassEntityManager.h"
#include "Net/UnrealNetwork.h"
#include "MassExecutionContext.h"

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FSimpleRandomMoveClientBubbleHandler::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	UE_LOG(LogTemp, Log, TEXT("SimpleRandomMove: PostReplicatedAdd called with %d entities (FinalSize=%d)"), AddedIndices.Num(), FinalSize);

	auto AddRequirementsForSpawnQuery = [this](FMassEntityQuery& InQuery)
	{
		TransformHandler.AddRequirementsForSpawnQuery(InQuery);
	};

	auto CacheFragmentViewsForSpawnQuery = [this](FMassExecutionContext& InExecContext)
	{
		TransformHandler.CacheFragmentViewsForSpawnQuery(InExecContext);
	};

	auto SetSpawnedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedSimpleRandomMoveAgent& ReplicatedEntity, const int32 EntityIdx)
	{
		TransformHandler.SetSpawnedEntityData(EntityIdx, ReplicatedEntity.GetReplicatedPositionYawData());
	};

	auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedSimpleRandomMoveAgent& Item)
	{
		PostReplicatedChangeEntity(EntityView, Item);
	};

	PostReplicatedAddHelper(AddedIndices, AddRequirementsForSpawnQuery, CacheFragmentViewsForSpawnQuery, SetSpawnedEntityData, SetModifiedEntityData);

	TransformHandler.ClearFragmentViewsForSpawnQuery();
}
#endif //UE_REPLICATION_COMPILE_CLIENT_CODE

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FSimpleRandomMoveClientBubbleHandler::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	auto SetModifiedEntityData = [this](const FMassEntityView& EntityView, const FReplicatedSimpleRandomMoveAgent& Item)
	{
		PostReplicatedChangeEntity(EntityView, Item);
	};

	PostReplicatedChangeHelper(ChangedIndices, SetModifiedEntityData);
}
#endif //UE_REPLICATION_COMPILE_CLIENT_CODE

#if UE_REPLICATION_COMPILE_CLIENT_CODE
void FSimpleRandomMoveClientBubbleHandler::PostReplicatedChangeEntity(const FMassEntityView& EntityView, const FReplicatedSimpleRandomMoveAgent& Item) const
{
	// Update the entity's transform from the replicated position/yaw data
	TMassClientBubbleTransformHandler<FSimpleRandomMoveFastArrayItem>::SetModifiedEntityData(EntityView, Item.GetReplicatedPositionYawData());
}
#endif // UE_REPLICATION_COMPILE_CLIENT_CODE

//----------------------------------------------------------------------//
// ASimpleRandomMoveClientBubbleInfo
//----------------------------------------------------------------------//

ASimpleRandomMoveClientBubbleInfo::ASimpleRandomMoveClientBubbleInfo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Serializers.Add(&Serializer);
}

void ASimpleRandomMoveClientBubbleInfo::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SharedParams;
	SharedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(ASimpleRandomMoveClientBubbleInfo, Serializer, SharedParams);
}
