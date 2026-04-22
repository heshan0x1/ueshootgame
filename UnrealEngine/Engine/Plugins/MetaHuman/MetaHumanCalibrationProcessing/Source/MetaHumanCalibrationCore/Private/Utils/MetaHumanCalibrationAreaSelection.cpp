// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/MetaHumanCalibrationAreaSelection.h"

void FMetaHumanCalibrationAreaSelection::OnStart(const FVector2D& InStartingPosition)
{
	StartingPosition = InStartingPosition;
}

void FMetaHumanCalibrationAreaSelection::OnUpdate(const FVector2D& InNewPosition)
{
	AreaOfInterest = FSlateRect(
		FMath::Min(StartingPosition.X, InNewPosition.X),
		FMath::Min(StartingPosition.Y, InNewPosition.Y),
		FMath::Max(StartingPosition.X, InNewPosition.X),
		FMath::Max(StartingPosition.Y, InNewPosition.Y)
	);
}

FSlateRect FMetaHumanCalibrationAreaSelection::OnEnd(const FVector2D& InLastPosition)
{
	AreaOfInterest = FSlateRect(
		FMath::Min(StartingPosition.X, InLastPosition.X),
		FMath::Min(StartingPosition.Y, InLastPosition.Y),
		FMath::Max(StartingPosition.X, InLastPosition.X),
		FMath::Max(StartingPosition.Y, InLastPosition.Y)
	);

	FSlateRect LastAreaOfInterest = AreaOfInterest;

	AreaOfInterest = FSlateRect();

	return LastAreaOfInterest;
}

void FMetaHumanCalibrationAreaSelection::OnDraw(const FGeometry& InAllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 InPaintOnLayerId) const
{
	if (AreaOfInterest.IsValid())
	{
		FVector2D ToDrawTopLeft = FVector2D::Clamp(AreaOfInterest.GetTopLeft(), FVector2D::ZeroVector, InAllottedGeometry.GetLocalSize());
		FVector2D ToDrawBotRight = FVector2D::Clamp(AreaOfInterest.GetBottomRight(), FVector2D::ZeroVector, InAllottedGeometry.GetLocalSize());

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			InPaintOnLayerId,
			InAllottedGeometry.ToPaintGeometry(ToDrawBotRight - ToDrawTopLeft, FSlateLayoutTransform(ToDrawTopLeft)),
			FAppStyle::GetBrush(TEXT("MarqueeSelection"))
		);
	}
}