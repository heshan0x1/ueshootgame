// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBrushTool.h"
#include "BaseTools/MeshSurfacePointMeshEditingTool.h"
#include "MeshDescription.h"
#include "DynamicMesh/DynamicVerticesOctree3.h"
#include "BoneWeights.h"
#include "DynamicSubmesh3.h"
#include "GroupTopology.h"
#include "SkeletalMeshAttributes.h"
#include "Misc/Optional.h"
#include "Containers/Map.h"
#include "DynamicMesh/DynamicMeshOctree3.h"
#include "SkeletalMesh/SkeletalMeshEditingInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Selections/GeometrySelection.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"
#include "ToolMeshSelector.h"
#include "ToolTargetManager.h"

#include "SkinWeightsPaintTool.generated.h"

#define UE_API MESHMODELINGTOOLSEDITORONLY_API


class USkeletalMeshComponentReadOnlyToolTarget;
struct FMeshDescription;
class USkinWeightsPaintTool;
class UPolygonSelectionMechanic;
class UPersonaEditorModeManagerContext;
class FEditorViewportClient;
class UVolumetricBrushStampIndicator;

namespace UE::Geometry 
{
	struct FGeometrySelection;
	template <typename BoneIndexType, typename BoneWeightType> class TBoneWeightsDataSource;
	template <typename BoneIndexType, typename BoneWeightType> class TSmoothBoneWeights;
}

using BoneIndex = int32;

// weight edit mode
UENUM()
enum class EWeightEditMode : uint8
{
	Brush,
	Mesh,
	Bones,
};

// weight transfers happen between a source and target
UENUM()
enum class EMeshTransferOption : uint8
{
	Source,
	Target,
};

// weight color mode
UENUM()
enum class EWeightColorMode : uint8
{
	Greyscale,
	Ramp,
	BoneColors,
	FullMaterial,
};

// brush falloff mode
UENUM()
enum class EWeightBrushFalloffMode : uint8
{
	Surface,
	Volume,
};

// operation type when editing weights
UENUM()
enum class EWeightEditOperation : uint8
{
	Add,
	Replace,
	Multiply,
	Relax,
	RelativeScale
};

// mirror direction mode
UENUM()
enum class EMirrorDirection : uint8
{
	PositiveToNegative,
	NegativeToPositive,
};

namespace SkinPaintTool
{
	struct FSkinToolWeights;

	struct FVertexBoneWeight
	{
		FVertexBoneWeight() : BoneID(INDEX_NONE), VertexInBoneSpace(FVector::ZeroVector), Weight(0.0f) {}
		FVertexBoneWeight(BoneIndex InBoneIndex, const FVector& InPosInRefPose, float InWeight) :
			BoneID(InBoneIndex), VertexInBoneSpace(InPosInRefPose), Weight(InWeight){}
		
		BoneIndex BoneID;
		FVector VertexInBoneSpace;
		float Weight;
	};

	using VertexWeights = TArray<FVertexBoneWeight, TFixedAllocator<UE::AnimationCore::MaxInlineBoneWeightCount>>;

	// data required to preview the skinning deformations as you paint
	struct FSkinToolDeformer
	{
		void Initialize(const FReferenceSkeleton& InRefSkeleton, const TArray<FTransform>& PoseComponentSpace, const FDynamicMesh3& InMesh);

		void SetAllVerticesToBeUpdated();

		void SetToRefPose(USkinWeightsPaintTool* Tool);

		void UpdateVertexDeformation(USkinWeightsPaintTool* Tool, const TArray<FTransform>& PoseComponentSpace);

		void SetVertexNeedsUpdated(int32 VertexIndex);
		
		// which vertices require updating (partially re-calculated skinning deformation while painting)
		TSet<int32> VerticesWithModifiedWeights;
		// position of all vertices in the reference pose
		TArray<FVector> RefPoseVertexPositions;
		// Store a ref to the Mesh so there is a way to iterate over valid verts
		const FDynamicMesh3* Mesh;
		// inverted, component space ref pose transform of each bone
		TArray<FTransform> InvCSRefPoseTransforms;
		// bones transforms used in last deformation update
		TArray<FTransform> PreviousPoseComponentSpace;
		// bones transforms stored for duration of async deformation update
		TArray<FTransform> RefPoseComponentSpace;
		// bone index to bone name
		TArray<FName> BoneNames;
		TMap<FName, BoneIndex> BoneNameToIndexMap;
		
		FReferenceSkeleton RefSkeleton;
		
	};

	// store a sparse set of modifications to a set of vertex weights on a SINGLE bone
	struct FSingleBoneWeightEdits
	{
		int32 BoneIndex;
		
		TMap<VertexIndex, float> OldWeights;
		TMap<VertexIndex, float> NewWeights;
		
		TArray<VertexIndex> VerticesAddedTo;
		TArray<VertexIndex> VerticesRemovedFrom;
	};

	// store a sparse set of modifications to a set of vertex weights for a SET of bones
	// with support for merging edits. these are used for transaction history undo/redo.
	struct FMultiBoneWeightEdits
	{
		void MergeSingleEdit(
			const int32 BoneIndex,
			const int32 VertexID,
			const float NewWeight,
			bool bPruneInfluence,
			const TArray<VertexWeights>& InPreChangeWeights);
		void MergeEdits(const FSingleBoneWeightEdits& BoneWeightEdits);
		float GetVertexDeltaFromEdits(const int32 BoneIndex, const int32 VertexIndex);
		void AddEditedVerticesToSet(TSet<int32>& OutEditedVertexSet) const;
		
		// map of bone indices to weight edits made to that bone
		TMap<BoneIndex, FSingleBoneWeightEdits> PerBoneWeightEdits;
	};
	
	class FMeshSkinWeightsChange : public FToolCommandChange
	{
	public:
		FMeshSkinWeightsChange(const FName InSkinWeightProfile)
			: FToolCommandChange()
			, SkinWeightProfile(InSkinWeightProfile)
		{}

		virtual FString ToString() const override
		{
			return FString(TEXT("Edit Skin Weights"));
		}

		virtual void Apply(UObject* Object) override;

		virtual void Revert(UObject* Object) override;

		void StoreBoneWeightEdit(const FSingleBoneWeightEdits& BoneWeightEdit, const TFunction<int32(int32)>& VertexIndexConverter);
		
		void StoreMultipleWeightEdits(const FMultiBoneWeightEdits& WeightEdits, const TFunction<int32(int32)>& VertexIndexConverter);

	private:
		FMultiBoneWeightEdits AllWeightEdits;
		FName SkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
		
	};

	// intermediate storage of the weight maps for duration of tool
	struct FSkinToolWeights
	{
		// copy the initial weight values from the skeletal mesh
		void InitializeSkinWeights(
			USkinWeightsPaintTool* Tool,
			const FDynamicMesh3& Mesh);

		// applies an edit to a single vertex weight on a single bone, then normalizes the remaining weights while
		// keeping the edited weight intact (ie, adapts OTHER influences to achieve normalization)
		void CreateWeightEditForVertex(
			const int32 BoneIndex,
			const int32 VertexId,
			float NewWeightValue,
			FMultiBoneWeightEdits& WeightEdits);

		void ApplyCurrentWeightsToMesh(FDynamicMesh3& Mesh, TFunction<int32(int32)> WeightsIndexToMeshIndex = {});
		
		static float GetWeightOfBoneOnVertex(
			const int32 BoneIndex,
			const int32 VertexID,
			const TArray<VertexWeights>& InVertexWeights);

		static void FillWeightEdit(
			const int32 BoneIndex,
			const int32 VertexID,
			const float NewWeight,
			const TArray<VertexWeights>& InVertexWeights);

		void SetWeightOfBoneOnVertex(
			const int32 BoneIndex,
			const int32 VertexID,
			const float Weight,
			TArray<VertexWeights>& InOutVertexData);

		void RemoveInfluenceFromVertex(
			const VertexIndex VertexID,
			const BoneIndex BoneID,
			TArray<VertexWeights>& InOutVertexWeights);

		void AddNewInfluenceToVertex(
			const VertexIndex VertexID,
			const BoneIndex BoneIndex,
			const float Weight,
			TArray<VertexWeights>& InOutVertexWeights);

		// some weight editing operations are RELATIVE to existing weights before the change started (Multiply, Add etc)
		// these "existing weights" are stored in the PreChangeWeights buffer
		// PreChange and Current buffers must be synchronized after a transaction
		void SyncWeightBuffers();

		float SetCurrentFalloffAndGetMaxFalloffThisStroke(int32 VertexID, float CurrentStrength);

		void ApplyEditsToCurrentWeights(const FMultiBoneWeightEdits& Edits);

		void UpdateIsBoneWeighted(BoneIndex BoneToUpdate);

		BoneIndex GetParentBoneToWeightTo(BoneIndex ChildBone);
		
		// double-buffer of the entire weight matrix (stored sparsely for fast deformation)
		// "Pre" is state of weights at stroke start
		// "Current" is state of weights during stroke
		// When stroke is over, PreChangeWeights are synchronized with CurrentWeights
		TArray<VertexWeights> PreChangeWeights;
		TArray<VertexWeights> CurrentWeights;

		// record the current maximum amount of falloff applied to each vertex during the current stroke
		// values range from 0-1, this allows brushes to sweep over the same vertex, and apply only the maximum amount
		// of modification (add/replace/relax etc) that was encountered for the duration of the stroke.
		TArray<float> MaxFalloffPerVertexThisStroke;

		// record which bones have any weight assigned to them
		TArray<bool> IsBoneWeighted;

		// update deformation when vertex weights are modified
		FSkinToolDeformer Deformer;

		// which skin profile is currently edited
		FName Profile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
	};

	struct FSkinMirrorData
	{
		// lazily updates the mirror data tables for the current skeleton/mesh/mirror plane
		void EnsureMirrorDataIsUpdated(
			const TArray<FName>& BoneNames,
			const TMap<FName, BoneIndex>& BoneNameToIndexMap,
			const FReferenceSkeleton& RefSkeleton,
			const TArray<FVector>& RefPoseVertices,
			EAxis::Type InMirrorAxis,
			EMirrorDirection InMirrorDirection);

		// get a map of Target > Source bone ids across the current mirror plane
		const TMap<int32, int32>& GetBoneMap() const { return BoneMap; };
		// get the map of Target > Source vertex ids across the current mirror plane
		const TMap<int32, int32>& GetVertexMap() const;
		// return true if the point lies on the TARGET side of the mirror plane
		bool IsPointOnTargetMirrorSide(const FVector& InPoint) const;
		// forces mirror tables to be re-generated (do this after any mesh change operation)
		void SetNeedsReinitialized() {bIsInitialized = false;};
		
	private:
		
		bool bIsInitialized = false;
		TEnumAsByte<EAxis::Type> Axis;
		EMirrorDirection Direction; 
		TMap<int32, int32> BoneMap;
		TMap<int32, int32> VertexMap; // <Target, Source>
	};

	
}

UCLASS(MinimalAPI)
class USkinWeightsPaintToolBuilder : public UMeshSurfacePointMeshEditingToolBuilder
{
	GENERATED_BODY()

public:
	UE_API UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;

protected:
	UE_API virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

// for saveing/restoring the brush settings separately for each brush mode (Add, Replace, etc...)
USTRUCT()
struct FSkinWeightBrushConfig
{
	GENERATED_BODY()
	
	UPROPERTY()
	float Strength = 1.f;

	UPROPERTY()
	float Radius = 20.0f;
	
	UPROPERTY()
	float Falloff = 1.0f;

	UPROPERTY()
	EWeightBrushFalloffMode FalloffMode = EWeightBrushFalloffMode::Surface;
};

struct FDirectEditWeightState
{
	EWeightEditOperation EditMode;
	float StartValue = 0.f;
	float CurrentValue = 0.f;
	bool bInTransaction = false;

	UE_API void Reset();
	UE_API float GetModeDefaultValue();
	UE_API float GetModeMinValue();
	UE_API float GetModeMaxValue();
};

// Container for properties displayed in Details panel while using USkinWeightsPaintTool
UCLASS(MinimalAPI, config = EditorSettings)
class USkinWeightsPaintToolProperties : public UBrushBaseProperties
{
	GENERATED_BODY()

	UE_API USkinWeightsPaintToolProperties();
	
public:

	// brush vs selection modes
	UPROPERTY(Config)
	EWeightEditMode EditingMode;

	// custom brush modes and falloff types
	UPROPERTY(Config)
	EWeightEditOperation BrushMode;
	EWeightEditOperation PriorBrushMode; // when toggling with modifier key

	// are we selecting vertices, edges or faces
	UPROPERTY(Config)
	EComponentSelectionMode ComponentSelectionMode = EComponentSelectionMode::Faces;

	// weight color properties
	UPROPERTY(EditAnywhere, Config, Category = MeshDisplay)
	EWeightColorMode ColorMode;
	UPROPERTY(EditAnywhere, Config, Category = MeshDisplay)
	TArray<FLinearColor> ColorRamp;

	// weight editing arguments
	UPROPERTY(Config)
	TEnumAsByte<EAxis::Type> MirrorAxis = EAxis::X;
	UPROPERTY(Config)
	EMirrorDirection MirrorDirection = EMirrorDirection::PositiveToNegative;
	UPROPERTY(Config)
	float PruneValue = 0.01;
	UPROPERTY(Config)
	int32 ClampValue = 8;
	UPROPERTY(Config)
	int32 ClampSelectValue = 8;
	UPROPERTY(Config)
	float AddStrength = 1.0;
	UPROPERTY(Config)
	float ReplaceValue = 1.0;
	UPROPERTY(Config)
	float RelaxStrength = 0.5;
	UPROPERTY(Config)
	float AverageStrength = 1.0;
	// the state of the direct weight editing tools (mode buttons + slider)
	FDirectEditWeightState DirectEditState;

	// save/restore user specified settings for each tool mode
	UE_API FSkinWeightBrushConfig& GetBrushConfig();
	TMap<EWeightEditOperation, FSkinWeightBrushConfig*> BrushConfigs;
	UPROPERTY(Config)
	FSkinWeightBrushConfig BrushConfigAdd;
	UPROPERTY(Config)
	FSkinWeightBrushConfig BrushConfigReplace;
	UPROPERTY(Config)
	FSkinWeightBrushConfig BrushConfigMultiply;
	UPROPERTY(Config)
	FSkinWeightBrushConfig BrushConfigRelax;

	UPROPERTY(EditAnywhere, Category = SkinWeightLayer, meta = (DisplayName = "Active Profile", GetOptions = GetTargetSkinWeightProfilesFunc))
	FName ActiveSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
	
	// new profile properties
	UPROPERTY(meta = (TransientToolProperty))
	bool bShowNewProfileName = false;
	UPROPERTY(EditAnywhere, Category = SkinWeightLayer, meta = (TransientToolProperty, DisplayName = "New Profile Name",
		EditCondition = bShowNewProfileName, HideEditConditionToggle, NoResetToDefault))
	FName NewSkinWeightProfile = "Profile";

	UE_API FName GetActiveSkinWeightProfile() const;
	
	// pointer back to paint tool
	TObjectPtr<USkinWeightsPaintTool> WeightTool;

	UE_API void SetComponentMode(EComponentSelectionMode InComponentMode);
	UE_API void SetFalloffMode(EWeightBrushFalloffMode InFalloffMode);
	UE_API void SetColorMode(EWeightColorMode InColorMode);
	UE_API void SetBrushMode(EWeightEditOperation InBrushMode);

	// transfer
	UPROPERTY(EditAnywhere, Transient, Category = "WeightTransfer")
	TWeakObjectPtr<USkeletalMesh> SourceSkeletalMesh;
	UPROPERTY(EditAnywhere, Transient, Category = "WeightTransfer")
	EMeshTransferOption MeshSelectMode = EMeshTransferOption::Target;
	UPROPERTY(EditAnywhere, Category = "WeightTransfer", meta = (GetOptions = GetSourceLODsFunc))
	FName SourceLOD = "LOD0";
	UPROPERTY(EditAnywhere, Category = "WeightTransfer", meta = (DisplayName = "Source Profile", GetOptions = GetSourceSkinWeightProfilesFunc))
	FName SourceSkinWeightProfile = FSkeletalMeshAttributesShared::DefaultSkinWeightProfileName;
	UPROPERTY(EditAnywhere, Transient, Category = "WeightTransfer")
	FTransform SourcePreviewOffset = FTransform::Identity;
	
private:
	UFUNCTION()
	UE_API TArray<FName> GetTargetSkinWeightProfilesFunc() const;
	UFUNCTION()
	UE_API TArray<FName> GetSourceLODsFunc() const;
	UFUNCTION()
	UE_API TArray<FName> GetSourceSkinWeightProfilesFunc() const;
};

// this class wraps the all the components to enable selection on a single mesh in the skin weights tool
// this allows us to make selections on multiple different meshes
// NOTE: at some point we may want to do component selections on multiple meshes in any/all viewports
// at which time this class should be centralized and renamed to UMeshSelector or something like that.
// But there will need to be some sort of centralized facility to manage that and make sure it interacts nicely with other tools.
UCLASS(MinimalAPI, Deprecated)
class UDEPRECATED_WeightToolMeshSelector : public UToolMeshSelector
{
	GENERATED_BODY()
};

// this class wraps a source skeletal mesh used to transfer skin weights to the tool target mesh
UCLASS(MinimalAPI)
class UWeightToolTransferManager : public UObject
{
	GENERATED_BODY()

public:

	// this must be called from within the parent tool's Setup() so that the selection mechanics are registered for capturing input
	UE_API void InitialSetup(USkinWeightsPaintTool* InWeightTool, FEditorViewportClient* InViewportClient);
	
	// called when the tool is shutdown
	UE_API void Shutdown();

	// render the selection mechanism
	UE_API void Render(IToolsContextRenderAPI* RenderAPI);
	UE_API void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI);

	// update the mesh we are transferring from
	UE_API void SetSourceMesh(USkeletalMesh* InSkeletalMesh = nullptr);

	// run the weight transfer
	UE_API void TransferWeights();

	// returns true if everything is setup and ready to transfer
	UE_API bool CanTransferWeights() const;
	
	// gets the tool target for the source mesh
	UToolTarget* GetTarget() const { return SourceTarget; }

	// get the preview mesh for the source mesh
	UPreviewMesh* GetPreviewMesh() const { return SourcePreviewMesh; };
	
	// get the mesh selector for the source mesh
	UToolMeshSelector* GetMeshSelector() const { return MeshSelector; };
	
	// called when tool settings are modified
	UE_API void OnPropertyModified(const USkinWeightsPaintToolProperties* WeightToolProperties, const FProperty* ModifiedProperty);

private:
	
	// actually run the weight transfer to copy weights from the source to the target
	UE_API void TransferWeightsFromOtherMeshOrSubset();

	// actually run the weight transfer to copy weights from the source to the target
	UE_API void TransferWeightsFromSameMeshAndLOD();

	UE_API void ApplyTranferredWeightsAsTransaction(
		const UE::Geometry::FDynamicMeshVertexSkinWeightsAttribute* InTransferredSkinWeights,
		const TArray<int32>& InVertexSubset,
		const FDynamicMesh3& InTargetMesh);
	
	UPROPERTY()
	TObjectPtr<UPreviewMesh> SourcePreviewMesh = nullptr;
	UPROPERTY()
	TObjectPtr<USkeletalMesh> SourceSkeletalMesh = nullptr;
	UPROPERTY()
	TObjectPtr<UToolTarget> SourceTarget = nullptr;
	UPROPERTY()
	TObjectPtr<UToolMeshSelector> MeshSelector;
	TWeakObjectPtr<USkinWeightsPaintTool> WeightTool;
};

// this class wraps all the data needed to isolate a selection of a mesh while editing skin weights
UCLASS(MinimalAPI)
class UWeightToolSelectionIsolator : public UObject
{
	GENERATED_BODY()

public:

	// call during tool Setup()
	UE_API void InitialSetup(USkinWeightsPaintTool* InTool);

	// call every tick to apply deferred changes to mesh
	UE_API void UpdateIsolatedSelection();

	// returns true if any triangles are currently isolated
	UE_API bool IsSelectionIsolated() const;

	// isolate the current selection
	UE_API void IsolateSelectionAsTransaction();

	// unisolate the current selection
	UE_API void UnIsolateSelectionAsTransaction();

	// isolate the array of triangles
	UE_API void SetTrianglesToIsolate(const TArray<int32>& TrianglesToIsolate);
	
	// restores the whole mesh
	UE_API void RestoreFullMesh();
	
	// get the current triangles that are isolated
	const TArray<int32>& GetIsolatedTriangles() { return CurrentlyIsolatedTriangles; };

	// convert to/from partial-isolated and full mesh vertex indices
	UE_API int32 PartialToFullMeshVertexIndex(int32 PartialMeshVertexIndex) const;
	UE_API int32 FullToPartialMeshVertexIndex(int32 FullMeshVertexIndex) const;

	// returns the isolated partial mesh (if PartialMeshDescription is not null, this will return an empty DynamicMesh3)
	UE_API const FDynamicMesh3& GetPartialMesh() const;

private:

	UE_API void CreatePartialMesh();
	
	UPROPERTY()
	TObjectPtr<USkinWeightsPaintTool> WeightTool;
		
	// when selection is isolated, we hide the full mesh and show a submesh
	// when islated selection is unhidden, we remap all changes from the submesh back to the full mesh
	TArray<int32> CurrentlyIsolatedTriangles;
	UE::Geometry::FGeometrySelection IsolatedSelectionToRestoreVertices;
	UE::Geometry::FGeometrySelection IsolatedSelectionToRestoreEdges;
	UE::Geometry::FGeometrySelection IsolatedSelectionToRestoreFaces;
	bool bIsolatedMeshNeedsUpdated = false;
	// isolate selection sub-meshes
	UE::Geometry::FDynamicSubmesh3 PartialSubMesh;
};

class FIsolateSelectionChange : public FToolCommandChange
{
public:
	TArray<int32> IsolatedTrianglesBefore;
	TArray<int32> IsolatedTrianglesAfter;

	UE_API virtual void Apply(UObject* Object) override;
	UE_API virtual void Revert(UObject* Object) override;
	UE_API virtual FString ToString() const override;
};



// An interactive tool for painting and editing skin weights.
UCLASS(MinimalAPI)
class USkinWeightsPaintTool : public UDynamicMeshBrushTool, public ISkeletalMeshEditingInterface
{
	GENERATED_BODY()

public:
	// Derived tools can optionally override these if they are invoked in editors that don't offer skeletal mesh editor context object
	UE_API virtual EMeshLODIdentifier GetEditingLOD();
	UE_API virtual const TArray<FTransform>& GetComponentSpaceBoneTransforms();
	UE_API virtual void ToggleBoneManipulation(bool bEnable);
	

	// UBaseBrushTool overrides
	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }
	UE_API virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	UE_API virtual void OnBeginDrag(const FRay& Ray) override;
	UE_API virtual void OnUpdateDrag(const FRay& Ray) override;
	UE_API virtual void OnEndDrag(const FRay& Ray) override;
	UE_API virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	UE_API virtual double EstimateMaximumTargetDimension() override;
	UE_API virtual void SetupBrushStampIndicator() override;
	UE_API virtual void UpdateBrushStampIndicator() override;
	UE_API virtual void ShutdownBrushStampIndicator() override;

	UE_API void Init(const FToolBuilderState& InSceneState);
	
	// UInteractiveTool
	UE_API virtual void Setup() override;
	UE_API virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	UE_API virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	// IInteractiveToolCameraFocusAPI
	virtual bool SupportsWorldSpaceFocusBox() override { return true; }
	UE_API virtual FBox GetWorldSpaceFocusBox() override;

	// IClickDragBehaviorTarget implementation
	UE_API virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	UE_API virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& InPressPos) override;

	// using when ToolChange is applied via Undo/Redo
	UE_API void ExternalUpdateWeights(const int32 BoneIndex, const TMap<int32, float>& IndexValues);
	UE_API void ExternalUpdateSkinWeightLayer(const FName InSkinWeightProfile);
	UE_API void ExternalAddInfluenceToVertices(const BoneIndex InfluenceToAdd, const TArray<VertexIndex>& Vertices);
	UE_API void ExternalRemoveInfluenceFromVertices(const BoneIndex InfluenceToRemove, const TArray<VertexIndex>& Vertices);

	// weight editing operations (selection based)
	UE_API void MirrorWeights(EAxis::Type Axis, EMirrorDirection Direction);
	UE_API void PruneWeights(const float Threshold, const TArray<BoneIndex>& BonesToPrune);
	UE_API void AverageWeights(const float Strength);
	UE_API void NormalizeWeights();
	UE_API void HammerWeights();
	UE_API void ClampInfluences(const int32 MaxInfluences);

	// HELPER functions for modifying weights
	//
	// given a map of BoneIndex > Weight values for a single vertex, modify the map by removing the smallest weights to fit in Max Influences
	static UE_API void TruncateWeightMap(TMap<BoneIndex, float>& InOutWeights);
	// given a map of BoneIndex > Weight values for a single vertex, modify the weights to sum to 1
	static UE_API void NormalizeWeightMap(TMap<BoneIndex, float>& InOutWeights);
	// sum all the weights on all bones for a given list of vertices (results we not be normalized!)
	static UE_API void AccumulateWeights(
		const TArray<SkinPaintTool::VertexWeights>& AllWeights,
		const TArray<VertexIndex>& VerticesToAccumulate,
		TMap<BoneIndex, float>& OutWeights);

	// copy paste
	UE_API void CopyWeights();
	UE_API void PasteWeights();
	static UE_API const FString CopyPasteWeightsIdentifier;
	
	// method to set weights directly (numeric input, for example)
	UE_API void EditWeightsOnVertices(
		BoneIndex Bone,
		const float Value,
		const int32 Iterations,
		EWeightEditOperation EditOperation,
		const TArray<VertexIndex>& VerticesToEdit,
		const bool bShouldTransact);

	// toggle brush / selection mode
	UE_API void ToggleEditingMode();
	// update the state of the mesh selectors
	UE_API void UpdateSelectorState() const;
	
	// get access to the mesh selector for the main mesh
	UE_API UToolMeshSelector* GetMainMeshSelector();
	// get access to the currently active mesh selector (may be on the transfer source mesh)
	UE_API UToolMeshSelector* GetActiveMeshSelector();
	// does the main mesh have an active selection ("active" meaning the selection is currently being rendered in the view and is editable)
	UE_API bool HasActiveSelectionOnMainMesh();
	// select all vertices affected by the currently selected bone(s)
	UE_API void SelectAffected() const;
	// select all vertices affected by at least MinInfluences number of bones
	UE_API void SelectByInfluenceCount(const int32 MinInfluences) const;

	// get the average weight value of each influence on the given vertices
	UE_API void GetInfluences(const TArray<int32>& VertexIndices, TArray<BoneIndex>& OutBoneIndices);
	// get the average weight value of a single bone on the given vertices
	UE_API float GetAverageWeightOnBone(const BoneIndex InBoneIndex, const TArray<int32>& VertexIndices);
	// convert an index to a name
	UE_API FName GetBoneNameFromIndex(BoneIndex InIndex) const;
	// get the currently selected bone
	UE_API BoneIndex GetCurrentBoneIndex() const;
	// get a list of vertices affected by the given bone
	UE_API void GetVerticesAffectedByBone(BoneIndex IndexOfBone, TSet<int32>& OutVertexIndices) const;

	// toggle the display of weights on the preview mesh (if false, uses the normal skeletal mesh material)
	UE_API void SetDisplayVertexColors(bool bShowVertexColors=true);
	// set focus back to viewport so that hotkeys are immediately detected while hovering
	UE_API void SetFocusInViewport() const;

	// get the target manager (cached from Setup)
	UToolTargetManager* GetTargetManager() const { return TargetManager.Get(); };

	// Set the target manager 
	void SetTargetManager(UToolTargetManager* InTargetManager) { TargetManager = InTargetManager; };

	// allows outside systems to access the weight data
	SkinPaintTool::FSkinToolWeights& GetWeights() { return Weights; };

	// get access to the weight tranfer system
	UWeightToolTransferManager* GetWeightTransferManager() const { return TransferManager; };

	// get the viewport this tool is operating in
	UE_API virtual FEditorViewportClient* GetViewportClient() const;
	
	// get access to the selection isolation system
	UWeightToolSelectionIsolator* GetSelectionIsolator() const { return SelectionIsolator; };

	// get the tool properties
	UE_API USkinWeightsPaintToolProperties* GetWeightToolProperties() const;

	// get access to the mesh being edited
	UE_API FDynamicMesh3& GetMesh() {return EditedMesh;};

	// HOW TO EDIT WEIGHTS WITH UNDO/REDO:
	//
	// "Interactive" Edits:
	// For multiple weight editing operations that need to be grouped into a single transaction, like dragging a slider or
	// dragging a brush, you must call:
	//  1. BeginChange()
	//  2. ApplyWeightEditsWithoutTransaction() (this may be called multiple times)
	//  2. EndChange()
	// All the edits are stored into the "ActiveChange" and applied as a single transaction in EndChange().
	// Deformations and vertex colors will be updated throughout the duration of the change.
	UE_API void BeginChange();
	UE_API void EndChange(const FText& TransactionLabel);
	UE_API void ApplyWeightEditsWithoutTransaction(const SkinPaintTool::FMultiBoneWeightEdits& WeightEdits);
	// "One-off" Edits:
	// For all one-and-done edits, you can call ApplyWeightEditsAsTransaction().
	// It will Begin/End the change and create a transaction for it.
	UE_API void ApplyWeightEditsAsTransaction(const SkinPaintTool::FMultiBoneWeightEdits& WeightEdits, const FText& TransactionLabel);

	// call this whenever the target mesh is modified
	UE_API void UpdateCurrentlyEditedMesh(const FDynamicMesh3& InDynamicMesh);

	// Update the brush indicators visibility state
	UE_API void UpdateBrushIndicators();

	// called whenever the selection is modified
	DECLARE_MULTICAST_DELEGATE(FOnSelectionChanged);
	FOnSelectionChanged OnSelectionChanged;

	// called whenever the weights are modified
	DECLARE_MULTICAST_DELEGATE(FOnWeightsChanged);
	FOnWeightsChanged OnWeightsChanged;

protected:

	UE_API virtual void ApplyStamp(const FBrushStampData& Stamp);
	UE_API void OnShutdown(EToolShutdownType ShutdownType) override;
	UE_API void OnTick(float DeltaTime) override;

	// stamp
	UE_API float CalculateBrushFalloff(float Distance) const;
	UE_API void CalculateVertexROI(
		const FBrushStampData& InStamp,
		TArray<VertexIndex>& OutVertexIDs,
		TArray<float>& OutVertexFalloffs);
	UE_API float CalculateBrushStrengthToUse(EWeightEditOperation EditMode) const;
	bool bInvertStroke = false;
	FBrushStampData StartStamp;
	FBrushStampData LastStamp;
	bool bStampPending;
	int32 TriangleUnderStamp;
	FVector StampLocalPos;
	
	// generating bone weight edits to be stored in a transaction
	// does not actually change the weight buffers
	UE_API void CreateWeightEditsForVertices(
		EWeightEditOperation EditOperation,
		const BoneIndex Bone,
		const TArray<int32>& VerticesToEdit,
		const TArray<float>& VertexFalloffs,
		const float InValue,
		SkinPaintTool::FMultiBoneWeightEdits& InOutWeightEdits);
	// same as CreateWeightEditsForVertices() but specific to relaxation (topology aware operation)
	UE_API void CreateWeightEditsToRelaxVertices(
		TArray<int32> VerticesToEdit,
		TArray<float> VertexFalloffs,
		const float Strength,
		const int32 Iterations,
		SkinPaintTool::FMultiBoneWeightEdits& InOutWeightEdits);

	// used to accelerate mesh queries
	using DynamicVerticesOctree = UE::Geometry::TDynamicVerticesOctree3<FDynamicMesh3>;
	TUniquePtr<DynamicVerticesOctree> VerticesOctree;
	using DynamicTrianglesOctree = UE::Geometry::FDynamicMeshOctree3;
	TUniquePtr<DynamicTrianglesOctree> TrianglesOctree;
	TFuture<void> TriangleOctreeFuture;
	TArray<int32> TrianglesToReinsert;
	UE_API void InitializeOctrees();

	// tool properties
	UPROPERTY()
	TObjectPtr<USkinWeightsPaintToolProperties> WeightToolProperties;
	UE_API virtual void OnPropertyModified(UObject* ModifiedObject, FProperty* ModifiedProperty) override;
	
	// the currently edited mesh descriptions
	FDynamicMesh3 EditedMesh;

	// storage of vertex weights per bone 
	SkinPaintTool::FSkinToolWeights Weights;

	// cached mirror data
	SkinPaintTool::FSkinMirrorData MirrorData;

	// storage for weight edits in the current transaction
	TUniquePtr<SkinPaintTool::FMeshSkinWeightsChange> ActiveChange;

	// Smooth weights data source and operator
	TUniquePtr<UE::Geometry::TBoneWeightsDataSource<int32, float>> SmoothWeightsDataSource;
	TUniquePtr<UE::Geometry::TSmoothBoneWeights<int32, float>> SmoothWeightsOp;
	UE_API void InitializeSmoothWeightsOperator();

	// vertex colors updated when switching current bone or initializing whole mesh
	UE_API void UpdateVertexColorForAllVertices();
	bool bVertexColorsNeedUpdated = false;
	// vertex colors updated when make sparse edits to subset of vertices
	UE_API void UpdateVertexColorForSubsetOfVertices();
	TSet<int32> VerticesToUpdateColor;
	UE_API FVector4f GetColorOfVertex(VertexIndex InVertexIndex, BoneIndex InBoneIndex) const;

	// which bone are we currently painting?
	UE_API void UpdateCurrentBone(const FName &BoneName);
	UE_API BoneIndex GetBoneIndexFromName(const FName BoneName) const;
	FName CurrentBone = NAME_None;
	TOptional<FName> PendingCurrentBone;
	TArray<FName> SelectedBoneNames;
	TArray<BoneIndex> SelectedBoneIndices;

	// ISkeletalMeshEditingInterface
	UE_API virtual void HandleSkeletalMeshModified(const TArray<FName>& InBoneNames, const ESkeletalMeshNotifyType InNotifyType) override;

	// the selection system for the main mesh
	UPROPERTY()
	TObjectPtr<UToolMeshSelector> MeshSelector;

	// skin weight layer
	UE_API void OnActiveSkinWeightProfileChanged();
	UE_API void OnNewSkinWeightProfileChanged();
	UE_API bool IsProfileValid(const FName InProfileName);

	// global properties stored on initialization
	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshEditorContextObjectBase> EditorContext = nullptr;
	UPROPERTY()
	TWeakObjectPtr<UPersonaEditorModeManagerContext> PersonaModeManagerContext = nullptr;
	UPROPERTY()
	TWeakObjectPtr<UToolTargetManager> TargetManager = nullptr;

	// manages transferring skin weights from a separate mesh
	UPROPERTY()
	TObjectPtr<UWeightToolTransferManager> TransferManager = nullptr;

	// manages isolating a selection of the mesh
	UPROPERTY()
	TObjectPtr<UWeightToolSelectionIsolator> SelectionIsolator = nullptr;
	
	// editor state to restore when exiting the paint tool
	FString PreviewProfileToRestore;

	TArray<FTransform> DefaultComponentSpaceBoneTransforms;

	friend SkinPaintTool::FSkinToolDeformer;
};

#undef UE_API
