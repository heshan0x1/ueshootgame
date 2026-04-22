// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshDescription.h"
#include "GroupTopology.h"
#include "Templates/UniquePtr.h"
#include "UObject/ObjectPtr.h"

#include "Math/MathFwd.h"

#include "ToolMeshSelector.generated.h"

#define UE_API MESHMODELINGTOOLSEDITORONLY_API

class FCanvas;
class FEditorViewportClient;
class IToolsContextRenderAPI;
class UInteractiveTool;
class UPolygonSelectionMechanic;
class UPreviewMesh;
class UWorld;

// component selection mode
UENUM()
enum class EComponentSelectionMode : uint8
{
	Vertices,
	Edges,
	Faces
};

using VertexIndex = int32;

// this class wraps the all the components to enable selection on a single mesh in the skin weights tool
// this allows us to make selections on multiple different meshes
// NOTE: at some point we may want to do component selections on multiple meshes in any/all viewports
// at which time this class should be centralized and renamed to UMeshSelector or something like that.
// But there will need to be some sort of centralized facility to manage that and make sure it interacts nicely with other tools.
UCLASS(MinimalAPI)
class UToolMeshSelector : public UObject
{
	GENERATED_BODY()

public:

	// must be called during the Setup of the parent tool
	UE_DEPRECATED(5.7, "Use InitialSetup that does not need use a Viewport Client")
	UE_API void InitialSetup(UWorld* InWorld, UInteractiveTool* InParentTool, FEditorViewportClient* InViewportClient, TFunction<void()> OnSelectionChangedFunc);
	UE_API void InitialSetup(UWorld* InWorld, UInteractiveTool* InParentTool, TFunction<void()> OnSelectionChangedFunc);

	// must be called AFTER InitialSetup, and any time the mesh is changed
	// passing in a null preview mesh will disable the selector
	UE_API void SetMesh(
		UPreviewMesh* InMesh,
		const FTransform3d& InMeshTransform);

	UE_API void UpdateAfterMeshDeformation();

	UE_API void Shutdown();

	UE_API void SetIsEnabled(bool bIsEnabled);
	UE_API void SetComponentSelectionMode(EComponentSelectionMode InMode);
	UE_API void SetTransform(const FTransform3d& InTargetTransform);

	// viewport 
	UE_API void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);
	UE_API void Render(IToolsContextRenderAPI* RenderAPI);

	// get a list of currently selected vertices (converting edges and faces to vertices)
	UE_API const TArray<int32>& GetSelectedVertices();
	UE_API bool IsAnyComponentSelected() const;
	UE_API void GetSelectedTriangles(TArray<int32>& OutTriangleIndices) const;

	// edit selection
	UE_API void GrowSelection() const;
	UE_API void ShrinkSelection() const;
	UE_API void FloodSelection() const;
	UE_API void SelectBorder() const;

	// get access to the selection mechanic
	UPolygonSelectionMechanic* GetSelectionMechanic() { return PolygonSelectionMechanic; };

private:

	UPROPERTY()
	TObjectPtr<UInteractiveTool> ParentTool;
	UPROPERTY()
	TObjectPtr<UWorld> World;
	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh;
	UPROPERTY()
	TObjectPtr<UPolygonSelectionMechanic> PolygonSelectionMechanic;

	TUniquePtr<UE::Geometry::FDynamicMeshAABBTree3> MeshSpatial = nullptr;
	TUniquePtr<UE::Geometry::FTriangleGroupTopology> SelectionTopology = nullptr;

	TArray<VertexIndex> SelectedVerticesInternal;
};

#undef UE_API
