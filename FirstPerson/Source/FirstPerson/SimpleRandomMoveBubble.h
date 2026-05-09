// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassReplicationTransformHandlers.h"
#include "SimpleRandomMoveReplicatedAgent.h"
#include "MassClientBubbleHandler.h"
#include "MassClientBubbleInfoBase.h"
#include "MassEntityView.h"
#include "SimpleRandomMoveBubble.generated.h"

/**
 * Client bubble handler for SimpleRandomMove entities.
 * Handles how replicated data is applied on the client.
 */
class FSimpleRandomMoveClientBubbleHandler : public TClientBubbleHandlerBase<FSimpleRandomMoveFastArrayItem>
{
public:
	typedef TClientBubbleHandlerBase<FSimpleRandomMoveFastArrayItem> Super;
	typedef TMassClientBubbleTransformHandler<FSimpleRandomMoveFastArrayItem> FMassClientBubbleTransformHandler;

	FSimpleRandomMoveClientBubbleHandler()
		: TransformHandler(*this)
	{}

#if UE_REPLICATION_COMPILE_SERVER_CODE
	const FMassClientBubbleTransformHandler& GetTransformHandler() const { return TransformHandler; }
	FMassClientBubbleTransformHandler& GetTransformHandlerMutable() { return TransformHandler; }
#endif // UE_REPLICATION_COMPILE_SERVER_CODE

protected:
#if UE_REPLICATION_COMPILE_CLIENT_CODE
	virtual void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize) override;
	virtual void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize) override;

	void PostReplicatedChangeEntity(const FMassEntityView& EntityView, const FReplicatedSimpleRandomMoveAgent& Item) const;
#endif //UE_REPLICATION_COMPILE_CLIENT_CODE

	FMassClientBubbleTransformHandler TransformHandler;
};

/** Mass client bubble serializer - handles FastArray net delta serialization */
USTRUCT()
struct FSimpleRandomMoveClientBubbleSerializer : public FMassClientBubbleSerializerBase
{
	GENERATED_BODY()

	FSimpleRandomMoveClientBubbleSerializer()
	{
		Bubble.Initialize(Agents, *this);
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FSimpleRandomMoveFastArrayItem, FSimpleRandomMoveClientBubbleSerializer>(Agents, DeltaParams, *this);
	}

public:
	FSimpleRandomMoveClientBubbleHandler Bubble;

protected:
	UPROPERTY(Transient)
	TArray<FSimpleRandomMoveFastArrayItem> Agents;
};

template<>
struct TStructOpsTypeTraits<FSimpleRandomMoveClientBubbleSerializer> : public TStructOpsTypeTraitsBase2<FSimpleRandomMoveClientBubbleSerializer>
{
	enum
	{
		WithNetDeltaSerializer = true,
		WithCopy = false,
	};
};

/**
 * The info actor that provides actual replication per client.
 * There is one per PlayerController, and it owns the serializer.
 */
UCLASS()
class FIRSTPERSON_API ASimpleRandomMoveClientBubbleInfo : public AMassClientBubbleInfoBase
{
	GENERATED_BODY()

public:
	ASimpleRandomMoveClientBubbleInfo(const FObjectInitializer& ObjectInitializer);

	FSimpleRandomMoveClientBubbleSerializer& GetSerializer() { return Serializer; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated, Transient)
	FSimpleRandomMoveClientBubbleSerializer Serializer;
};
