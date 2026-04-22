// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateIMInGameWidgetBase.h"

#include "SlateIMInGameWindow.generated.h"

#define UE_API SLATEIMINGAME_API

UCLASS(MinimalAPI)
class ASlateIMInGameWindow : public ASlateIMInGameWidgetBase
{
	GENERATED_BODY()
public:
	ASlateIMInGameWindow(const FObjectInitializer& ObjectInitializer);
	UE_API ASlateIMInGameWindow(const FName& InWindowName, const FStringView& InWindowTitle);

protected:
	UE_API virtual void DrawWidget(const float DeltaTime) override;

	virtual void DrawContent(const float DeltaTime) {};

	FName WindowName;
	FString WindowTitle;
	FVector2f WindowSize = FVector2f(500,500);
	bool bDestroyRequested = false;
};

#undef UE_API
