// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "UIFEvents.generated.h"


class APlayerController;
class UUIFrameworkWidget;

/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFrameworkSimpleEventArgument
{
	GENERATED_BODY()

	FUIFrameworkSimpleEventArgument() = default;

	UPROPERTY()
	TObjectPtr<APlayerController> PlayerController;

	UPROPERTY()
	TObjectPtr<UUIFrameworkWidget> Sender;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUIFrameworkSimpleEvent, FUIFrameworkSimpleEventArgument);

/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFrameworkClickEventArgument : public FUIFrameworkSimpleEventArgument
{
	GENERATED_BODY()

	FUIFrameworkClickEventArgument() = default;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUIFrameworkClickEvent, FUIFrameworkClickEventArgument);

/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFrameworkFocusEventArgument : public FUIFrameworkSimpleEventArgument
{
	GENERATED_BODY()

	FUIFrameworkFocusEventArgument() = default;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUIFrameworkFocusEvent, FUIFrameworkFocusEventArgument);

/**
 *
 */
USTRUCT(BlueprintType)
struct FUIFrameworkHoverEventArgument : public FUIFrameworkSimpleEventArgument
{
	GENERATED_BODY()

	FUIFrameworkHoverEventArgument() = default;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUIFrameworkHoverEvent, FUIFrameworkHoverEventArgument);