// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowEditorTools/DataflowEditorWeightMapPaintBrushOps.h"
#include "Async/ParallelFor.h"
#include "DynamicMesh/DynamicMesh3.h"
//
// Paint Brush
//

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowEditorWeightMapPaintBrushOps)

void FDataflowWeightMapPaintBrushOp::ApplyStampByVertices(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& NewAttributesOut)
{
	UDataflowWeightMapPaintBrushOpProps* const Props = GetPropertySetAs<UDataflowWeightMapPaintBrushOpProps>();
	const double TargetValue = (double)Props->GetAttribute();

	const int32 NumVertices = Vertices.Num();
	ParallelFor(NumVertices, [&](int32 k)
	{
		if (bApplyRadiusLimit)
		{
			const FVector3d& StampPos = Stamp.LocalFrame.Origin;
			const int32 VertIdx = Vertices[k];
			const FVector3d VertexPos = Mesh->GetVertex(VertIdx);
			const double DistanceSquared = (VertexPos - StampPos).SquaredLength();
			if (DistanceSquared >= Stamp.Radius * Stamp.Radius)
			{
				return;
			}
		}

		const double ExistingValue = NewAttributesOut[k];
		const double ValueDiff = TargetValue - ExistingValue;
		NewAttributesOut[k] = FMath::Clamp(ExistingValue + Stamp.Power * ValueDiff, 0.0, 1.0);
	});
}


//
// Erase Brush
//

void FDataflowWeightMapEraseBrushOp::ApplyStampByVertices(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& NewAttributesOut)
{
	UDataflowWeightMapEraseBrushOpProps* Props = GetPropertySetAs<UDataflowWeightMapEraseBrushOpProps>();
	const double EraseAttribute = (double)Props->GetAttribute();

	// TODO: Add something here to get the old value so we can subtract (clamped) the AttributeValue from it.
	// TODO: Handle the stamp's properties for fall off, etc..

	check(NewAttributesOut.Num() == Vertices.Num());

	for (int32 k = 0; k < Vertices.Num(); ++k)
	{
		if (bApplyRadiusLimit)
		{
			const FVector3d& StampPos = Stamp.LocalFrame.Origin;
			const int32 VertIdx = Vertices[k];
			const FVector3d VertexPos = Mesh->GetVertex(VertIdx);
			const double DistanceSquared = (VertexPos - StampPos).SquaredLength();
			if (DistanceSquared >= Stamp.Radius * Stamp.Radius)
			{
				continue;
			}
		}
		NewAttributesOut[k] = EraseAttribute;
	}
}


//
// Smooth Brush
//

void FDataflowWeightMapSmoothBrushOp::ApplyStampByVertices(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& VertexWeightValues)
{
	ApplyStampByVerticesStatic(Mesh, Stamp, Vertices, VertexWeightValues, bApplyRadiusLimit);
}


void FDataflowWeightMapSmoothBrushOp::ApplyStampByVerticesStatic(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& VertexWeightValues,
	bool bApplyRadiusLimit)
{
	// Converted from FClothPaintTool_Smooth::SmoothVertices
	const int32 NumVertices = Vertices.Num();

	TMap<int32, int32> VertexToBufferIndexMap;
	for (int32 BufferIndex = 0; BufferIndex < NumVertices; ++BufferIndex)
	{
		const int32 VertexIndex = Vertices[BufferIndex];
		VertexToBufferIndexMap.Add(VertexIndex, BufferIndex);
	}

	// Compute average values of one-rings for all vertices
	TArray<double> OneRingAverages;
	OneRingAverages.SetNumUninitialized(NumVertices);

	const TMap<int32, int32>& VertexToBufferIndexMapConst = VertexToBufferIndexMap;
	const TArray<double>& VertexWeightValuesConst = VertexWeightValues;
	ParallelFor(NumVertices, 
		[&Vertices, &Mesh, &VertexToBufferIndexMapConst, &VertexWeightValuesConst, &OneRingAverages]
		(int32 BufferIndex)
		{
			const int32 VertexIndex = Vertices[BufferIndex];
			double Accumulator = 0.0f;
			int32 NumNeighbors = 0;

			for (const int32 NeighborIndex : Mesh->VtxVerticesItr(VertexIndex))
			{
				if (VertexToBufferIndexMapConst.Contains(NeighborIndex))
				{
					const int32 NeighborBufferIndex = VertexToBufferIndexMapConst[NeighborIndex];
					Accumulator += VertexWeightValuesConst[NeighborBufferIndex];
					++NumNeighbors;
				}
			}

			if (NumNeighbors > 0)
			{
				OneRingAverages[BufferIndex] = Accumulator / NumNeighbors;
			}
			else
			{
				// Don't change the vertex value if it has no neighbors
				OneRingAverages[BufferIndex] = VertexWeightValuesConst[BufferIndex];
			}
		}, 
		EParallelForFlags::None
	);

	// Blend vertex value with its average one-ring value
	ParallelFor(NumVertices,
		[&OneRingAverages, &VertexWeightValues, bApplyRadiusLimit, &Stamp, &Vertices, &Mesh](int32 BufferIndex)
		{
			const double Diff = OneRingAverages[BufferIndex] - VertexWeightValues[BufferIndex];
			if (bApplyRadiusLimit)
			{
				const FVector3d& StampPos = Stamp.LocalFrame.Origin;
				const int32 VertexIndex = Vertices[BufferIndex];
				const FVector3d VertexPos = Mesh->GetVertex(VertexIndex);
				const double DistanceSquared = (VertexPos - StampPos).SquaredLength();
				if (DistanceSquared >= Stamp.Radius * Stamp.Radius)
				{
					return;
				}
			}
			VertexWeightValues[BufferIndex] += Stamp.Power * Diff;
		}, 
		EParallelForFlags::None
	);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FDataflowVertexAttributePaintBrushOp::ApplyStampByVertices(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& OutValues)
{
	check(OutValues.Num() == Vertices.Num());

	if (UDataflowVertexAttributePaintBrushOpProps* Props = GetPropertySetAs<UDataflowVertexAttributePaintBrushOpProps>())
	{
		ApplyStampByVerticesStatic(Mesh, Stamp, Vertices, OutValues, Props->EditOperation, Props->GetAttribute(), bApplyRadiusLimit);
	}
}

/*static*/ void FDataflowVertexAttributePaintBrushOp::ApplyStampByVerticesStatic(
	const FDynamicMesh3* Mesh,
	const FSculptBrushStamp& Stamp,
	const TArray<int32>& Vertices,
	TArray<double>& OutValues,
	EDataflowEditorToolEditOperation EditOperation,
	const double ToolValue,
	bool bApplyRadiusLimit)
{
	// special case for Relax option 
	if (EditOperation == EDataflowEditorToolEditOperation::Relax)
	{
		FDataflowWeightMapSmoothBrushOp::ApplyStampByVerticesStatic(Mesh, Stamp, Vertices, OutValues, bApplyRadiusLimit);
		return;
	}

	// other operations
	const int32 NumVertices = Vertices.Num();
	ParallelFor(NumVertices, 
		[bApplyRadiusLimit, ToolValue, EditOperation , &Stamp, &Mesh, &Vertices, &OutValues]
		(int32 BufferIndex)
		{
			if (bApplyRadiusLimit)
			{
				const FVector3d& StampPos = Stamp.LocalFrame.Origin;
				const int32 MeshVertIdx = Vertices[BufferIndex];
				const FVector3d VertexPos = Mesh->GetVertex(MeshVertIdx);
				const double DistanceSquared = (VertexPos - StampPos).SquaredLength();
				if (DistanceSquared >= Stamp.Radius * Stamp.Radius)
				{
					return;
				}
			}

			const double CurrentValue = OutValues[BufferIndex];
			double NewValue = CurrentValue;

			switch (EditOperation)
			{
			case EDataflowEditorToolEditOperation::Add:
				NewValue = CurrentValue + ToolValue;
				break;
			case EDataflowEditorToolEditOperation::Replace:
				NewValue = ToolValue;
				break;
			case EDataflowEditorToolEditOperation::Multiply:
				NewValue = CurrentValue * ToolValue;
				break;
			case EDataflowEditorToolEditOperation::Invert:
				NewValue = 1.0 - CurrentValue;
				break;
			case EDataflowEditorToolEditOperation::Relax:
				ensure(false); // already handled by SmoothBrush.ApplyStampByVertices
			}

			OutValues[BufferIndex] = FMath::Clamp(NewValue, 0.0, 1.0);
		},
		EParallelForFlags::None
	);
}