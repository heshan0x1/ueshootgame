// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"

#include "WorkspaceViewportMenuContext.generated.h"

class UWorkspaceViewportSceneDescription;

UCLASS()
class UWorkspaceViewportMenuContext : public UObject
{
	GENERATED_BODY()

public:
	TAttribute<bool> bIsPinned;
	TAttribute<FSoftObjectPath> PreviewAssetPath;
	FSimpleDelegate OnPinnedClicked;

	UPROPERTY()
	TObjectPtr<UWorkspaceViewportSceneDescription> SceneDescription;
};