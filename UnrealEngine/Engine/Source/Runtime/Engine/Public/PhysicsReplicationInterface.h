// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/PhysicsObject.h"
#include "Physics/NetworkPhysicsSettingsComponent.h" // Temporary until RegisterSettings with FNetworkPhysicsSettingsAsync param is deprecated

class UPrimitiveComponent;
struct FRigidBodyState;
struct FNetworkPhysicsSettingsData;

class IPhysicsReplication // Game Thread API
{
public:
	virtual ~IPhysicsReplication() { }

	virtual void Tick(float DeltaSeconds) { }

	virtual void SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame) = 0;

	virtual void RemoveReplicatedTarget(UPrimitiveComponent* Component) = 0;
};

class IPhysicsReplicationAsync // Physics Thread API
{
public:
	virtual ~IPhysicsReplicationAsync() { }

	UE_DEPRECATED(5.7, "Deprecated, use the RegisterSettings that passes through a TWeakPtr<FNetworkPhysicsSettingsData> parameter instead ")
	virtual void RegisterSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject, FNetworkPhysicsSettingsAsync InSettings) { }

	virtual void RegisterSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject, TWeakPtr<const FNetworkPhysicsSettingsData> InSettings) = 0;
};
