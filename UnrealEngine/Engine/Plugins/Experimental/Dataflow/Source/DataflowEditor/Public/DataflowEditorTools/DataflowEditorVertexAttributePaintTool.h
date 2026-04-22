// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/BaseBrushTool.h"
#include "BaseTools/MeshSurfacePointMeshEditingTool.h"
#include "Components/DynamicMeshComponent.h"
#include "PropertySets/WeightMapSetProperties.h"
#include "Mechanics/PolyLassoMarqueeMechanic.h"

#include "Sculpting/MeshSculptToolBase.h"
#include "Sculpting/MeshBrushOpBase.h"

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshOctree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "DynamicMesh/MeshNormals.h"
#include "TransformTypes.h"
#include "ToolDataVisualizer.h"
#include "GroupTopology.h"
#include "MeshVertexPaintTool.h"
#include "DataflowEditorTools/DataflowEditorToolBuilder.h"
#include "Dataflow/DataflowColorRamp.h"
#include "DataflowEditorTools/DataflowEditorToolEnums.h"
#include "ToolMeshSelector.h"

#include "DataflowEditorVertexAttributePaintTool.generated.h"


#define UE_API DATAFLOWEDITOR_API

class IToolsContextTransactionsAPI;
class UMeshElementsVisualizer;
class UDataflowWeightMapSmoothBrushOpProps;
class UDataflowVertexAttributePaintBrushOpProps;
class UDataflowContextObject;
struct FDataflowVertexAttributeEditableNode;
struct FDataflowVertexAttributeProviderNode;
class UDataflowEditorMode;

namespace UE::DataflowEditorVertexAttributePaintTool::Private
{
	class FMeshChange;
}

namespace UE::DataflowEditorVertexAttributePaintTool::CVars
{
	extern bool DataflowEditorUseNewWeightMapTool;
}

DECLARE_STATS_GROUP(TEXT("Dataflow_VertexAttributePaintTool"), STATGROUP_VertexAttributePaintTool, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Dataflow_VertexAttributePaintTool_UpdateROI"), VertexAttributePaintTool_UpdateROI, STATGROUP_VertexAttributePaintTool);
DECLARE_CYCLE_STAT(TEXT("Dataflow_VertexAttributePaintTool_ApplyStamp"), VertexAttributePaintTool_ApplyStamp, STATGROUP_VertexAttributePaintTool);
DECLARE_CYCLE_STAT(TEXT("Dataflow_VertexAttributePaintTool_Tick"), VertexAttributePaintTool_Tick, STATGROUP_VertexAttributePaintTool);
DECLARE_CYCLE_STAT(TEXT("Dataflow_VertexAttributePaintTool_Tick_ApplyStampBlock"), VertexAttributePaintTool_Tick_ApplyStampBlock, STATGROUP_VertexAttributePaintTool);
DECLARE_CYCLE_STAT(TEXT("Dataflow_VertexAttributePaintTool_Tick_UpdateMeshBlock"), VertexAttributePaintTool_Tick_UpdateMeshBlock, STATGROUP_VertexAttributePaintTool);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Tool Builder
 */
UCLASS(MinimalAPI)
class UDataflowEditorVertexAttributePaintToolBuilder : public UMeshSurfacePointMeshEditingToolBuilder, public IDataflowEditorToolBuilder
{
	GENERATED_BODY()
public:
	void SetFallbackToolBuilder(UMeshSurfacePointMeshEditingToolBuilder* InFallbackToolBuilder) { FallbackToolBuilder = InFallbackToolBuilder; }
	void SetEditorMode(UDataflowEditorMode* InMode) { Mode = InMode; }

private:
	UE_API virtual void GetSupportedConstructionViewModes(const UDataflowContextObject& ContextObject, TArray<const UE::Dataflow::IDataflowConstructionViewMode*>& Modes) const override;
	UE_API virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	UE_API virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
	virtual bool CanSetConstructionViewWireframeActive() const { return false; }
	UDataflowEditorMode* Mode = nullptr;

	UPROPERTY()
	TObjectPtr<UMeshSurfacePointMeshEditingToolBuilder> FallbackToolBuilder = nullptr;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolBrushProperties
{
	GENERATED_BODY()

	UPROPERTY()
	EDataflowEditorToolEditOperation BrushMode = EDataflowEditorToolEditOperation::Replace;

	/** Adaptive size of brush - Relative to the model size */
	UPROPERTY()
	float BrushSize = 0.25f;

	/** Value to paint on the mesh (dependent on the BrushMode) */
	UPROPERTY()
	float AttributeValue = 1.0;

	/** Area Mode specifies the shape of the brush and which triangles will be included relative to the cursor */
	UPROPERTY(EditAnywhere, Category = Filters, meta = (DisplayName = "Brush Area Mode"))
	EMeshVertexPaintBrushAreaType BrushAreaMode = EMeshVertexPaintBrushAreaType::Connected;

	/** The Region affected by the current operation will be bounded by edge angles larger than this threshold */
	UPROPERTY(EditAnywhere, Category = Filters, meta = (UIMin = "0.0", ClampMin = "0.0", UIMax = "180.0", ClampMax = "180.0"))
	float AngleThreshold = 180.0f;

	/** The Region affected by the current operation will be bounded by UV borders/seams */
	UPROPERTY(EditAnywhere, Category = Filters)
	bool bUVSeams = false;

	/** The Region affected by the current operation will be bounded by Hard Normal edges/seams */
	UPROPERTY(EditAnywhere, Category = Filters)
	bool bNormalSeams = false;

	/** Control which triangles can be affected by the current operation based on visibility. Applied after all other filters. */
	UPROPERTY(EditAnywhere, Category = Filters)
	EDataflowEditorToolVisibilityType VisibilityFilter = EDataflowEditorToolVisibilityType::None;

	/** The weight value at the brush indicator */
	UPROPERTY(VisibleAnywhere, Transient, Category = Query, meta = (NoResetToDefault))
	double ValueAtBrush = 0;
};

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolBrushConfig
{
	GENERATED_BODY()

public:
	/** Adaptive size of brush - Relative to the model size */
	UPROPERTY(Config)
	float BrushSize = 0.25f;

	/** Value to paint on the mesh (dependent on the BrushMode) */
	UPROPERTY(Config)
	float Value = 1.0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolGradientProperties
{
	GENERATED_BODY()

	// Mesh selection mode (vertex, edge face)
	/** The Gradient upper limit value */
	UPROPERTY(EditAnywhere, Category = Gradient, meta = (UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))
	double GradientHighValue = 1.0;

	/** The Gradient lower limit value */
	UPROPERTY(EditAnywhere, Category = Gradient, meta = (UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))
	double GradientLowValue = 0.0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolSelectionProperties
{
	GENERATED_BODY()

	// Mesh selection mode (vertex, edge face)
	UPROPERTY()
	EComponentSelectionMode ComponentSelectionMode = EComponentSelectionMode::Vertices;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolDisplayProperties
{
	GENERATED_BODY()

	// Vertex color display mode (Greyscale, Ramp, ... )
	UPROPERTY()
	EDataflowEditorToolColorMode ColorMode = EDataflowEditorToolColorMode::Greyscale;

	// Color ramp to use when the color mode is set to "Ramp"
	UPROPERTY()
	FDataflowColorRamp ColorRamp;

	// Used by UI customization 
	FDataflowColorCurveOwner GreyScaleColorRamp;
	FDataflowColorCurveOwner WhiteColorRamp;
};

USTRUCT()
struct FDataflowEditorVertexAttributePaintToolMirrorProperties
{
	GENERATED_BODY()

	UPROPERTY()
	TEnumAsByte<EAxis::Type> MirrorAxis = EAxis::X;

	UPROPERTY()
	EDataflowEditorToolMirrorDirection MirrorDirection = EDataflowEditorToolMirrorDirection::PositiveToNegative;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UCLASS(MinimalAPI, config = EditorSettings)
class UDataflowEditorVertexAttributePaintToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

	UE_API UDataflowEditorVertexAttributePaintToolProperties();

public:

	// Edit mode (brush,mesh...)
	UPROPERTY()
	EDataflowEditorToolEditMode EditingMode = EDataflowEditorToolEditMode::Brush;
	
	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolBrushProperties BrushProperties;

	UPROPERTY()
	FDataflowEditorVertexAttributePaintToolGradientProperties GradientProperties;

	UPROPERTY()
	FDataflowEditorVertexAttributePaintToolSelectionProperties SelectionProperties;

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolDisplayProperties DisplayProperties;

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolMirrorProperties MirrorProperties;

	const FDataflowEditorVertexAttributePaintToolBrushConfig& GetBrushConfig(EDataflowEditorToolEditOperation BrushMode) const;
	void SetBrushConfig(EDataflowEditorToolEditOperation BrushMode, const FDataflowEditorVertexAttributePaintToolBrushConfig& BrushConfig);

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolBrushConfig BrushConfigAdd;

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolBrushConfig BrushConfigReplace;

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolBrushConfig BrushConfigMultiply;

	UPROPERTY(config)
	FDataflowEditorVertexAttributePaintToolBrushConfig BrushConfigRelax;

private:
	void CreateDefaultColorRamp();

};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FDataflowEditorVertexAttributePaintToolData
{
public:
	void Setup(FDynamicMesh3& InMesh, FDataflowVertexAttributeEditableNode* InNodeToUpdate, TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject);
	void Shutdown();

	void BeginChange(UDynamicMeshComponent* Component);
	TUniquePtr<UE::DataflowEditorVertexAttributePaintTool::Private::FMeshChange> EndChange(UDynamicMeshComponent* Component);
	void CancelChange();

	bool IsValid() const { return (ActiveWeightMap != nullptr); }

	float GetValue(int32 VertexIdx) const;
	float GetAverageValue(const TArray<int32>& Vertices) const;

	// lambda signature : float GetValue(UE::Geometry::FDynamicMeshWeightAttribute& AttributeMap, int32 AttributeIndex)
	template <typename TLambda>
	void SetValue(int32 VertexIdx, TLambda Lambda)
	{
		if (ActiveWeightMap)
		{
			if (bHaveDynamicMeshToWeightConversion)
			{
				for (const int32 Idx : WeightToDynamicMesh[DynamicMeshToWeight[VertexIdx]])
				{
					ActiveWeightEditChangeTracker->SaveVertexOneRingTriangles(Idx, true);
					const float Value = Lambda(*ActiveWeightMap, Idx);
					ActiveWeightMap->SetValue(Idx, &Value);
				}
			}
			else
			{
				ActiveWeightEditChangeTracker->SaveVertexOneRingTriangles(VertexIdx, true);
				const float Value = Lambda(*ActiveWeightMap, VertexIdx);
				ActiveWeightMap->SetValue(VertexIdx, &Value);
			}
		}
	}

	void UpdateNode(TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject, IToolsContextTransactionsAPI* TransactionAPI);

	bool HaveDynamicMeshToWeightConversion() const { return bHaveDynamicMeshToWeightConversion; }
	const TArray<int32>& GetDynamicMeshToWeight() const { return  DynamicMeshToWeight; }
	const TArray<TArray<int32>>& GetWeightToDynamicMesh() const { return WeightToDynamicMesh; }

private:
	// DynamicMesh might be unwelded mesh, but weights are on the welded mesh.
	bool bHaveDynamicMeshToWeightConversion = false;
	TArray<int32> DynamicMeshToWeight;
	TArray<TArray<int32>> WeightToDynamicMesh;

	// Corresponding node to read from and write back to
	FDataflowVertexAttributeEditableNode* NodeToUpdate = nullptr;

	UE::Geometry::FDynamicMeshWeightAttribute* ActiveWeightMap = nullptr;

	TUniquePtr<UE::Geometry::FDynamicMeshChangeTracker> ActiveWeightEditChangeTracker;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Dataflow Editor tool to paint a vertex attribute as vertex colors
 */
UCLASS(MinimalAPI)
class UDataflowEditorVertexAttributePaintTool : public UMeshSculptToolBase
{
	GENERATED_BODY()

public:
	UE_API virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	UE_API virtual void Setup() override;
	UE_API virtual void Shutdown(EToolShutdownType ShutdownType) override;
	UE_API virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	UE_API virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	UE_API virtual void UpdateMaterialMode(EMeshEditingMaterialModes MaterialMode) override;
	UE_API virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	UE_API virtual bool CanAccept() const override;

	UE_API virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	UE_API virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	UE_API void SetBrushMode(EDataflowEditorToolEditOperation BrushMode);
	UE_API bool IsInBrushMode() const;
	UE_API bool IsVolumetricBrush() const;

	UE_API virtual void CommitResult(UBaseDynamicMeshComponent* Component, bool bModifiedTopology) override;

	UE_API void SetDataflowEditorContextObject(TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject);
	void SetEditorMode(UDataflowEditorMode* InMode) { Mode = InMode; }

	virtual void SetupBrushEditBehaviorSetup(ULocalTwoAxisPropertyEditInputBehavior& OutBehavior) override;

	//~ UObject interface

	static UE_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

public:
	/** properties of the tool */
	UPROPERTY()
	TObjectPtr<UDataflowEditorVertexAttributePaintToolProperties> ToolProperties;

	UPROPERTY()
	TObjectPtr<UDataflowVertexAttributePaintBrushOpProps> PaintBrushOpProperties;

	UPROPERTY()
	TObjectPtr<UDataflowWeightMapSmoothBrushOpProps> SmoothBrushOpProperties;

	UE_API bool HaveVisibilityFilter() const;
	UE_API void ApplyVisibilityFilter(const TArray<int32>& Vertices, TArray<int32>& VisibleVertices);
	UE_API void ApplyVisibilityFilter(TSet<int32>& Triangles, TArray<int32>& ROIBuffer, TArray<int32>& OutputBuffer);

	// we override these so we can update the separate BrushSize property added for this tool
	UE_API virtual void IncreaseBrushRadiusAction();
	UE_API virtual void DecreaseBrushRadiusAction();
	UE_API virtual void IncreaseBrushRadiusSmallStepAction();
	UE_API virtual void DecreaseBrushRadiusSmallStepAction();

	UE_API float GetBrushMinRadius() const;
	UE_API float GetBrushMaxRadius() const;

	UE_API void SetColorMode(EDataflowEditorToolColorMode NewColorMode);

	UE_API bool HasSelection() const;
	UE_API void GrowSelection() const;
	UE_API void ShrinkSelection() const;
	UE_API void FloodSelection() const;
	UE_API void SelectBorder() const;
	UE_API void SetComponentSelectionMode(EComponentSelectionMode NewMode);

	UE_API void CopyAverageFromSelectionToClipboard();
	UE_API void PasteValueToSelectionFromClipboard();
	UE_API void PruneSelection(float Threshold);

	// selection operations
	UE_API void ApplyValueToSelection(EDataflowEditorToolEditOperation Operation, float InValue);
	UE_API void MirrorValues();

	UE_API TArray<int32> GetSelectedVertices() const;

protected:
	// UMeshSculptToolBase API
	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() override;
	virtual FDynamicMesh3* GetBaseMesh()  override { check(false); return nullptr; }
	virtual const FDynamicMesh3* GetBaseMesh() const  override { check(false); return nullptr; }

	UE_API virtual int32 FindHitSculptMeshTriangleConst(const FRay3d& LocalRay) const override;
	UE_API virtual int32 FindHitTargetMeshTriangleConst(const FRay3d& LocalRay) const override;
	UE_API virtual void UpdateHitSculptMeshTriangle(int32 TriangleID, const FRay3d& LocalRay) override;

	UE_API virtual void OnBeginStroke(const FRay& WorldRay) override;
	UE_API virtual void OnEndStroke() override;
	UE_API virtual void OnCancelStroke() override;

	UE_API void OnColorRampChanged(TArray<FRichCurve*> Curves);

	virtual bool SharesBrushPropertiesChanges() const override { return false; }
	// end UMeshSculptToolBase API

protected:
	//
	// Marquee Support
	//
public:
	UPROPERTY()
	TObjectPtr<UPolyLassoMarqueeMechanic> PolyLassoMechanic;

protected:
	UE_API void OnPolyLassoFinished(const FCameraPolyLasso& Lasso, bool bCanceled);


	// 
	// Gradient Support
	//
	FToolDataVisualizer GradientSelectionRenderer;

	UE::Geometry::FGroupTopologySelection LowValueGradientVertexSelection;
	UE::Geometry::FGroupTopologySelection HighValueGradientVertexSelection;

	UE_API void ComputeGradient();
	UE_API void OnSelectionModified();

	//
	// Internals
	//

protected:

	UPROPERTY()
	TObjectPtr<UMeshElementsVisualizer> MeshElementsDisplay = nullptr;

	UPROPERTY()
	TObjectPtr<UDataflowContextObject> DataflowEditorContextObject = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh = nullptr;
	FMeshDescription PreviewMeshDescription;

	UPROPERTY()
	TObjectPtr<UToolMeshSelector> MeshSelector;

	// realtime visualization
	UE_API void OnDynamicMeshComponentChanged(UDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert);
	FDelegateHandle OnDynamicMeshComponentChangedHandle;

	UE_API float GetCurrentWeightValueUnderBrush() const;
	FVector3d CurrentBaryCentricCoords;
	UE_API int32 GetBrushNearestVertex() const;

	TSet<int32> AccumulatedTriangleROI;
	bool bUndoUpdatePending = false;
	TArray<int> NormalsBuffer;
	UE_API void WaitForPendingUndoRedoUpdate();

	TArray<int> TempROIBuffer;
	TArray<int> VertexROI;
	TArray<bool> VisibilityFilterBuffer;
	TSet<int> VertexSetBuffer;
	TSet<int> TriangleROI;
	UE_API void UpdateROI(const FSculptBrushStamp& CurrentStamp);

	UE_API bool UpdateStampPosition(const FRay& WorldRay);
	UE_API bool ApplyStamp();

	UE::Geometry::FDynamicMeshOctree3 Octree;

	UE_API void UpdateBrushType(EDataflowEditorToolEditOperation EditOperation);
	UE_API bool UpdateBrushPosition(const FRay& WorldRay);

	bool bPendingPickWeight = false;

	TArray<int32> ROITriangleBuffer;
	TArray<double> ROIWeightValueBuffer;
	UE_API bool SyncMeshWithWeightBuffer(FDynamicMesh3* Mesh);
	UE_API bool SyncWeightBufferWithMesh(const FDynamicMesh3* Mesh);
	UE_API void SetVerticesToWeightMap(const TSet<int32>& Vertices, double WeightValue);

	UE_API void BeginChange();
	UE_API void EndChange();
	UE_API void CancelChange();

	TArray<FVector3d> TriNormals;
	TArray<int32> UVSeamEdges;
	TArray<int32> NormalSeamEdges;
	UE_API void PrecomputeFilterData();

	FDataflowEditorVertexAttributePaintToolData VertexData;

protected:
	virtual bool ShowWorkPlane() const override { return false; }

	friend class UDataflowEditorVertexAttributePaintToolBuilder;

	bool bAnyChangeMade = false;

	/** update the vertex color of the mesh from the attribute */
	UE_API void UpdateVertexColorOverlay(const TSet<int>* TrianglesToUpdate = nullptr);

	UDataflowEditorMode* Mode = nullptr;

	static constexpr int32 PaintBrushId = 0;
	static constexpr int32 SmoothBrushId = 1;

private:
	// this structure holds information for mirroring 
	struct FMirrorData
	{
	public:
		// lazily updates the mirror data tables for the current skeleton/mesh/mirror plane
		void EnsureMirrorDataIsUpdated(const FDynamicMesh3& Mesh, EAxis::Type InMirrorAxis, EDataflowEditorToolMirrorDirection InMirrorDirection);

		void FindMirroredIndices(const FDynamicMesh3& Mesh, const TArray<int32>& SelectedVertices, TArray<int32>& OutVerticesToUpdate);

	private:
		// return true if the point lies on the TARGET side of the mirror plane
		bool IsPointOnTargetMirrorSide(const FVector& InPoint) const;

		EAxis::Type Axis = EAxis::X;
		EDataflowEditorToolMirrorDirection Direction = EDataflowEditorToolMirrorDirection::PositiveToNegative;
		TMap<int32, int32> VertexMap; // <Target, Source>
	};
	FMirrorData MirrorData;
};




#undef UE_API

