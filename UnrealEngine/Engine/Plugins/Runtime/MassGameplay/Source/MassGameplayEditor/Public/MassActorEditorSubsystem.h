// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "MassActorEditorSubsystem.generated.h"

#define UE_API MASSGAMEPLAYEDITOR_API


struct FMassActorManager;

UCLASS(MinimalAPI)
class UMassActorEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	FMassActorManager& GetMutableActorManager() { return *ActorManager.Get(); }

protected:
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	TSharedPtr<FMassActorManager> ActorManager;
};

#undef UE_API
