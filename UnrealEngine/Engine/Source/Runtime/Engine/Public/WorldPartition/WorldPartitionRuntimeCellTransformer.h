// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WorldPartitionRuntimeCellTransformer.generated.h"

UCLASS(MinimalAPI, Config=Engine)
class UWorldPartitionRuntimeCellTransformerSettings : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(Config)
	TArray<TSubclassOf<UActorComponent>> IgnoredComponentClasses;

	UPROPERTY(Config)
	TArray<TSubclassOf<UActorComponent>> IgnoredExactComponentClasses;
#endif
};

UCLASS(MinimalAPI, Abstract)
class UWorldPartitionRuntimeCellTransformer : public UObject
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	virtual void PreTransform(ULevel* InLevel) {}
	virtual void Transform(ULevel* InLevel) {}
	virtual void PostTransform(ULevel* InLevel) {}
	ENGINE_API virtual void ForEachIgnoredComponentClass(TFunctionRef<bool(const TSubclassOf<UActorComponent>&)> Func) const;
	ENGINE_API virtual void ForEachIgnoredExactComponentClass(TFunctionRef<bool(const TSubclassOf<UActorComponent>&)> Func) const;
#endif

	bool IsEnabled() const { return bEnabled; }

protected:
#if WITH_EDITOR
	ENGINE_API virtual bool CanIgnoreComponent(const UActorComponent* InComponent) const;
#endif
	
protected:
	// Tag used to force exclude actors from any cell transformation
	ENGINE_API static const FName NAME_CellTransformerIgnoreActor;

private:
	UPROPERTY(EditAnywhere, Category = Transformer)
	bool bEnabled = true;
};