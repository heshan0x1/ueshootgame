// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAreaOfInterest.h"

FSlateRect FMetaHumanAreaOfInterest::GetSlateRect() const
{
	return FSlateRect(TopLeft, BottomRight);
}

void FMetaHumanAreaOfInterest::SetFromSlateRect(const FSlateRect& InSlateRect)
{
	TopLeft = InSlateRect.GetTopLeft();
	BottomRight = InSlateRect.GetBottomRight();
}

FBox2D FMetaHumanAreaOfInterest::GetBox2D() const
{
	return FBox2D(TopLeft, BottomRight);
}

void FMetaHumanAreaOfInterest::SetFromBox2D(const FBox2D& InBox)
{
	TopLeft = InBox.Min;
	BottomRight = InBox.Max;
}