// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Layout/SlateRect.h"

#include "Math/Vector2D.h"
#include "Math/Box2D.h"

#include "MetaHumanAreaOfInterest.generated.h"

#define UE_API METAHUMANCALIBRATIONCORE_API

USTRUCT(BlueprintType, Blueprintable)
struct FMetaHumanAreaOfInterest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	FVector2D TopLeft = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options")
	FVector2D BottomRight = FVector2D::ZeroVector;

	UE_API FSlateRect GetSlateRect() const;
	UE_API void SetFromSlateRect(const FSlateRect& InSlateRect);
	UE_API FBox2D GetBox2D() const;
	UE_API void SetFromBox2D(const FBox2D& InBox);
};

#undef UE_API