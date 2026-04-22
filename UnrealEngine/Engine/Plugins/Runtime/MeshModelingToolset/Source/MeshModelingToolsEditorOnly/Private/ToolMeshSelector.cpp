// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolMeshSelector.h"

#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PreviewMesh.h"
#include "Selection/PolygonSelectionMechanic.h"

// thise method is deprecated - InViewportClient is no longer used 
void UToolMeshSelector::InitialSetup(UWorld* InWorld, UInteractiveTool* InParentTool, FEditorViewportClient* InViewportClient, TFunction<void()> OnSelectionChangedFunc)
{
	InitialSetup(InWorld, InParentTool, OnSelectionChangedFunc);
}

void UToolMeshSelector::InitialSetup(UWorld* InWorld, UInteractiveTool* InParentTool, TFunction<void()> OnSelectionChangedFunc)
{
	World = InWorld;

	// set up vertex selection mechanic
	PolygonSelectionMechanic = NewObject<UPolygonSelectionMechanic>(this);
	PolygonSelectionMechanic->bAddSelectionFilterPropertiesToParentTool = false;
	PolygonSelectionMechanic->Setup(InParentTool);
	PolygonSelectionMechanic->SetIsEnabled(false, false);
	PolygonSelectionMechanic->OnSelectionChanged.AddLambda(OnSelectionChangedFunc);

	// set up style of vertex selection
	constexpr FLinearColor VertexSelectedPurple = FLinearColor(0.78f, 0.f, 0.78f);
	constexpr FLinearColor VertexSelectedYellow = FLinearColor(1.f, 1.f, 0.f);
	// adjust selection rendering for this context
	PolygonSelectionMechanic->HilightRenderer.PointColor = FLinearColor::Blue;
	PolygonSelectionMechanic->HilightRenderer.PointSize = 10.0f;
	// vertex highlighting once selected
	PolygonSelectionMechanic->SelectionRenderer.LineThickness = 1.0f;
	PolygonSelectionMechanic->SelectionRenderer.PointColor = VertexSelectedYellow;
	PolygonSelectionMechanic->SelectionRenderer.PointSize = 5.0f;
	PolygonSelectionMechanic->SelectionRenderer.DepthBias = 2.0f;
	// despite the name, this renders the vertices
	PolygonSelectionMechanic->PolyEdgesRenderer.PointColor = VertexSelectedPurple;
	PolygonSelectionMechanic->PolyEdgesRenderer.PointSize = 5.0f;
	PolygonSelectionMechanic->PolyEdgesRenderer.DepthBias = 2.0f;
	PolygonSelectionMechanic->PolyEdgesRenderer.LineThickness = 1.0f;
}

void UToolMeshSelector::SetMesh(
	UPreviewMesh* InPreviewMesh,
	const FTransform3d& InMeshTransform)
{
	// store the mesh this is operating on
	PreviewMesh = InPreviewMesh;

	if (!ensure(World))
	{
		return;
	}

	if (!PreviewMesh)
	{
		SetIsEnabled(false);
		return;
	}

	// reset selection topology and mesh spatial data
	static constexpr bool bAutoBuild = true;
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
	SelectionTopology = MakeUnique<UE::Geometry::FTriangleGroupTopology>(DynamicMesh, bAutoBuild);
	MeshSpatial = MakeUnique<FDynamicMeshAABBTree3>(DynamicMesh, bAutoBuild);

	// initialize the selection mechanic
	PolygonSelectionMechanic->Initialize(
		DynamicMesh,
		InMeshTransform,
		World,
		SelectionTopology.Get(),
		[this]() { return MeshSpatial.Get(); }
	);

	// clear the selection (old selection is invalid on new topo)
	PolygonSelectionMechanic->ClearSelection();
	PolygonSelectionMechanic->ClearHighlight();

	// selection colors
	constexpr FLinearColor FaceSelectedOrange = FLinearColor(0.886f, 0.672f, 0.473f);
	// configure secondary render material for selected triangles
	// NOTE: the material returned by ToolSetupUtil::GetSelectionMaterial has a checkerboard pattern on back faces which makes it hard to use
	if (UMaterialInterface* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/SculptMaterial")))
	{
		if (UMaterialInstanceDynamic* MatInstance = UMaterialInstanceDynamic::Create(Material, World))
		{
			MatInstance->SetVectorParameterValue(TEXT("Color"), FaceSelectedOrange);
			PreviewMesh->SetSecondaryRenderMaterial(MatInstance);
		}
	}

	// secondary triangle buffer used to render face selection
	PreviewMesh->EnableSecondaryTriangleBuffers([this](const FDynamicMesh3* Mesh, int32 TriangleID)
		{
			return PolygonSelectionMechanic->GetActiveSelection().IsSelectedTriangle(Mesh, SelectionTopology.Get(), TriangleID);
		});
	// notify preview mesh when triangle selection has been updated
	PolygonSelectionMechanic->OnSelectionChanged.AddWeakLambda(this, [this]()
		{
			PreviewMesh->FastNotifySecondaryTrianglesChanged();
		});
	PolygonSelectionMechanic->OnFaceSelectionPreviewChanged.AddWeakLambda(this, [this]()
		{
			PreviewMesh->FastNotifySecondaryTrianglesChanged();
		});
}

void UToolMeshSelector::Shutdown()
{
	if (PolygonSelectionMechanic)
	{
		PolygonSelectionMechanic->Shutdown();
	}

	PolygonSelectionMechanic = nullptr;
}

void UToolMeshSelector::SetIsEnabled(bool bIsEnabled)
{
	if (!PolygonSelectionMechanic)
	{
		return;
	}

	// force off if there's no preview mesh
	bIsEnabled = PreviewMesh ? bIsEnabled : false;

	PolygonSelectionMechanic->SetIsEnabled(bIsEnabled, bIsEnabled);
}

void UToolMeshSelector::SetComponentSelectionMode(EComponentSelectionMode InMode)
{
	if (!(PolygonSelectionMechanic))
	{
		return;
	}

	PolygonSelectionMechanic->Properties->bSelectVertices = InMode == EComponentSelectionMode::Vertices;
	PolygonSelectionMechanic->Properties->bSelectEdges = InMode == EComponentSelectionMode::Edges;
	PolygonSelectionMechanic->Properties->bSelectFaces = InMode == EComponentSelectionMode::Faces;
	PolygonSelectionMechanic->SetShowSelectableCorners(InMode == EComponentSelectionMode::Vertices);
	PolygonSelectionMechanic->SetShowEdges(InMode == EComponentSelectionMode::Edges);
}

void UToolMeshSelector::SetTransform(const FTransform3d& InTargetTransform)
{
	if (PolygonSelectionMechanic)
	{
		PolygonSelectionMechanic->SetTransform(InTargetTransform);
	}
}

void UToolMeshSelector::UpdateAfterMeshDeformation()
{
	MeshSpatial->Build();
	constexpr bool bTopologyDeformed = true;
	constexpr bool bTopologyModified = false;
	PolygonSelectionMechanic->GetTopologySelector()->Invalidate(bTopologyDeformed, bTopologyModified);
}

void UToolMeshSelector::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (!PolygonSelectionMechanic)
	{
		return;
	}

	PolygonSelectionMechanic->DrawHUD(Canvas, RenderAPI);
}

void UToolMeshSelector::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!(PolygonSelectionMechanic && PreviewMesh))
	{
		return;
	}

	PolygonSelectionMechanic->Render(RenderAPI);
}

const TArray<int32>& UToolMeshSelector::GetSelectedVertices()
{
	SelectedVerticesInternal.Empty();
	if (!(PolygonSelectionMechanic && PreviewMesh))
	{
		return SelectedVerticesInternal;
	}

	const FGroupTopologySelection& Selection = PolygonSelectionMechanic->GetActiveSelection();
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();

	// validate and add vertices to the output array
	auto AddVertices = [this](const TSet<int32>& VerticesToAdd)
		{
			for (const int32 VertexToAdd : VerticesToAdd)
			{
				SelectedVerticesInternal.Add(VertexToAdd);
			}
		};

	// add selected vertices
	AddVertices(Selection.SelectedCornerIDs);

	// add vertices on selected edges
	{
		TSet<int32> VerticesInSelectedEdges;
		for (const int32 SelectedEdgeIndex : Selection.SelectedEdgeIDs)
		{
			FDynamicMesh3::FEdge CurrentEdge = DynamicMesh->GetEdge(SelectedEdgeIndex);
			VerticesInSelectedEdges.Add(CurrentEdge.Vert.A);
			VerticesInSelectedEdges.Add(CurrentEdge.Vert.B);
		}

		AddVertices(VerticesInSelectedEdges);
	}

	// add vertices in selected faces
	{
		TSet<int32> VerticesInSelectedFaces;
		for (const int32 SelectedFaceIndex : Selection.SelectedGroupIDs)
		{
			UE::Geometry::FIndex3i TriangleVertices = DynamicMesh->GetTriangleRef(SelectedFaceIndex);
			VerticesInSelectedFaces.Add(TriangleVertices[0]);
			VerticesInSelectedFaces.Add(TriangleVertices[1]);
			VerticesInSelectedFaces.Add(TriangleVertices[2]);
		}

		AddVertices(VerticesInSelectedFaces);
	}

	return SelectedVerticesInternal;
}

bool UToolMeshSelector::IsAnyComponentSelected() const
{
	if (!PolygonSelectionMechanic)
	{
		return false;
	}

	return PolygonSelectionMechanic->HasSelection();
}

void UToolMeshSelector::GetSelectedTriangles(TArray<int32>& OutTriangleIndices) const
{
	OutTriangleIndices.Empty();
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}

	const FGroupTopologySelection& Selection = PolygonSelectionMechanic->GetActiveSelection();
	const FDynamicMesh3* DynamicMesh = PreviewMesh->GetMesh();
	TSet<int32> TriangleSet;

	// add triangles connected to selected vertices
	for (const int32 VertexIndex : Selection.SelectedCornerIDs)
	{
		DynamicMesh->EnumerateVertexTriangles(VertexIndex, [&TriangleSet](int32 TriangleIndex)
			{
				TriangleSet.Add(TriangleIndex);
			});
	}

	// add triangles connected to selected edges
	for (const int32 EdgeIndex : Selection.SelectedEdgeIDs)
	{
		DynamicMesh->EnumerateEdgeTriangles(EdgeIndex, [&TriangleSet](int32 TriangleIndex)
			{
				TriangleSet.Add(TriangleIndex);
			});
	}

	// add selected triangles
	TriangleSet.Append(Selection.SelectedGroupIDs);

	OutTriangleIndices = TriangleSet.Array();
}

void UToolMeshSelector::GrowSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}

	PolygonSelectionMechanic->GrowSelection(/*bAsTriangleTopology*/ true);
}

void UToolMeshSelector::ShrinkSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}

	PolygonSelectionMechanic->ShrinkSelection(/*bAsTriangleTopology*/ true);
}

void UToolMeshSelector::FloodSelection() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}

	PolygonSelectionMechanic->FloodSelection();
}

void UToolMeshSelector::SelectBorder() const
{
	if (!ensure(PolygonSelectionMechanic))
	{
		return;
	}

	PolygonSelectionMechanic->ConvertSelectionToBorderVertices(/*bAsTriangleTopology*/ true);
}