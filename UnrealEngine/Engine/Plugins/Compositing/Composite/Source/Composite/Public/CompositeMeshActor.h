// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GameFramework/Actor.h"

#include "CompositeMeshActor.generated.h"

#define UE_API COMPOSITE_API

class UCompositeMeshComponent;

/** Convenience compositing mesh actor, which the composite actor can reference. */
UCLASS(MinimalAPI)
class ACompositeMeshActor : public AActor
{
	GENERATED_BODY()

public:
	UE_API ACompositeMeshActor(const FObjectInitializer& ObjectInitializer);
	UE_API ~ACompositeMeshActor();

private:
	/** Composite mesh component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Composite", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCompositeMeshComponent> CompositeMeshComponent;

public:
	/** Returns CompositeMeshComponent subobject **/
	UE_API class UCompositeMeshComponent* GetCaptureComponent2D() const { return CompositeMeshComponent; }
};

#undef UE_API
