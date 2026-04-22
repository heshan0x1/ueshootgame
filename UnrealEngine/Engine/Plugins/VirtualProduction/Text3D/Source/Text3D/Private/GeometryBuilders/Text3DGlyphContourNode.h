// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Logs/Text3DLogs.h"
#include "Polygon2.h"
#include "Templates/SharedPointer.h"

using UE::Geometry::FPolygon2f;

struct FText3DGlyphContourNode;
using TText3DGlyphContourNodeShared = TSharedPtr<FText3DGlyphContourNode>;

struct FText3DGlyphContourNode final
{
	explicit FText3DGlyphContourNode(const TSharedPtr<FPolygon2f> ContourIn, const bool bCanHaveIntersectionsIn, const bool bClockwiseIn)
		: Contour(ContourIn)
		, bCanHaveIntersections(bCanHaveIntersectionsIn)
		, bClockwise(bClockwiseIn)
	{
	}

	const TSharedPtr<FPolygon2f> Contour;

	// Needed for dividing contours with self-intersections: default value is true, false for parts of divided contours
	const bool bCanHaveIntersections;

	bool bClockwise;

	// Contours that are inside this contour
	TArray<TText3DGlyphContourNodeShared> Children;

	/** Debug print to display contour data */
	void Print(int32 InDepth = 0) const
	{
		const FString Indent = FString::ChrN(InDepth * 2, TEXT(' '));

		UE_LOG(LogText3D, Log, TEXT("%sContour: Vtx=%d, Area=%f, Bounds=%s, CW=%s"),
			*Indent,
			Contour.IsValid() ? Contour->VertexCount() : 0,
			Contour.IsValid() ? Contour->SignedArea() : 0,
			Contour.IsValid() ? *Contour->Bounds().Extents().ToString() : TEXT(""),
			bClockwise ? TEXT("true") : TEXT("false"));

		for (const TSharedPtr<FText3DGlyphContourNode>& Child : Children)
		{
			Child->Print(InDepth + 1);
		}
	}
};
