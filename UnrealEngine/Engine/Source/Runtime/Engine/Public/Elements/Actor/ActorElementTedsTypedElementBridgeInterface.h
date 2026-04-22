// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Elements/Interfaces/TedsTypedElementBridgeInterface.h"

#include "ActorElementTedsTypedElementBridgeInterface.generated.h"

UCLASS(MinimalAPI)
class UActorElementTedsTypedElementBridgeInterface : public UObject, public ITedsTypedElementBridgeInterface
{
	GENERATED_BODY()

public:
	virtual FTedsRowHandle GetRowHandle(const FTypedElementHandle& InElementHandle) const override;
};