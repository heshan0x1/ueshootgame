// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVLineBatchComponent.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawInterface.h"

FPVLineSceneProxy::FPVLineSceneProxy(const UPVLineBatchComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, LineComponent(InComponent)
{}

FPrimitiveViewRelevance FPVLineSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance ViewRelevance;
	ViewRelevance.bDrawRelevance = IsShown(View);
	ViewRelevance.bDynamicRelevance = true;
	ViewRelevance.bSeparateTranslucency = ViewRelevance.bNormalTranslucency = true;
	return ViewRelevance;
}

void FPVLineSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector
) const
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

#if UE_ENABLE_DEBUG_DRAWING
			FPrimitiveDrawInterface* PDI = Collector.GetDebugPDI(ViewIndex);
#else	
			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
#endif

			LineComponent->Draw(View, PDI);
		}
	}
}

SIZE_T FPVLineSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FPVLineSceneProxy::GetMemoryFootprint() const { return sizeof *this + GetAllocatedSize(); }

bool FPVLineSceneProxy::CanBeOccluded() const
{
	return false;
}

UPVLineBatchComponent::UPVLineBatchComponent()
{
	bAutoActivate = true;
	bTickInEditor = true;
	PrimaryComponentTick.bCanEverTick = true;
	bUseEditorCompositing = true;
	SetGenerateOverlapEvents(false);
	bIgnoreStreamingManagerUpdate = true;
	
	BBox = FBox(ForceInit);
	Bounds = FBoxSphereBounds(FVector::ZeroVector, FVector::OneVector, 1);
}

void UPVLineBatchComponent::InitBounds()
{
	BBox = FBox(ForceInit);
}

void UPVLineBatchComponent::AddLine(const FVector& InStartPos, const FVector& InEndPos, const FLinearColor& InColor,const ESceneDepthPriorityGroup InDepthPriorityGroup, const EPointDrawSettings InPointDrawSettings)
{
	Lines.Emplace(InStartPos, InEndPos, InColor, InDepthPriorityGroup, InPointDrawSettings);

	BBox += InStartPos;
	BBox += InEndPos;
}

void UPVLineBatchComponent::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) const
{
	for (const FPVLineInfo& Line : Lines)
	{
		PDI->DrawLine(
			Line.StartPos,
			Line.EndPos,
			Line.Color,
			Line.DepthPriorityGroup,
			3.0f,
			0,
			true
		);

		if (Line.PointDrawSettings == EPointDrawSettings::Start || Line.PointDrawSettings == EPointDrawSettings::Both)
		{
			PDI->DrawPoint(Line.StartPos, FLinearColor::White, PointSize, SDPG_Foreground);
		}

		if (Line.PointDrawSettings == EPointDrawSettings::End || Line.PointDrawSettings == EPointDrawSettings::Both)
		{
			PDI->DrawPoint(Line.EndPos, FLinearColor::White, PointSize, SDPG_Foreground);
		}
	}
}

void UPVLineBatchComponent::Flush()
{
	Lines.Empty();
	BBox = FBox(ForceInit);
}

FPrimitiveSceneProxy* UPVLineBatchComponent::CreateSceneProxy()
{
	FPVLineSceneProxy* Proxy = new FPVLineSceneProxy(this);
	return Proxy;
}

FBoxSphereBounds UPVLineBatchComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(BBox).TransformBy(LocalToWorld); 
}
