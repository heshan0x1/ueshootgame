// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "Engine/EngineBaseTypes.h"

#include "STrackerImageViewer.h"
#include "SMetaHumanOverlayWidget.h"

#define UE_API METAHUMANTOOLKIT_API

enum class EIdentityPoseType : uint8;
enum class EABImageViewMode;

/**
 * Struct for storing information about near and far plane for depth data.
 */
struct FMetaHumanViewportClientDepthData
{
	FMetaHumanViewportClientDepthData(float InNear, float InFar, float InNearMin, float InNearMax, float InNearFarDelta = 1.0f)
		: Near(InNear)
		, Far(InFar)
		, DepthRangeNear(TRange<float>::Inclusive(InNearMin, InNearMax))
		, NearFarDelta(InNearFarDelta)
	{
		check(Near < Far);
		check(InNearMin < InNearMax);
		check(NearFarDelta > 0.1f);
	}

	void SetNear(float InValue)
	{
		Near = FMath::Clamp(InValue, DepthRangeNear.GetLowerBoundValue(), DepthRangeNear.GetUpperBoundValue());
		SetFar(Far);
	}

	float GetNear() const
	{
		return Near;
	}

	void SetFar(float InValue)
	{
		Far = FMath::Clamp(InValue, Near + NearFarDelta, DepthRangeNear.GetUpperBoundValue() + NearFarDelta);
	}

	float GetFar() const
	{
		return Far;
	}

	TRange<float> GetRangeNear() const
	{
		return DepthRangeNear;
	}

	TRange<float> GetRangeFar() const
	{
		return TRange<float>::Inclusive(
			DepthRangeNear.GetLowerBoundValue() + NearFarDelta,
			DepthRangeNear.GetUpperBoundValue() + NearFarDelta
		);
	}

private:
	/** Near plane value */
	float Near = 10.0f;

	/** Far plane value */
	float Far = 55.0f;

	/** Valid range for near value */
	TRange<float> DepthRangeNear;

	/** Minimal allowed difference between far and near value */
	float NearFarDelta = 1.0f;
};

/**
 * The base class for viewport clients used in the MetaHuman asset editor viewports that need to support the AB split functionality and user manipulation of tracker contour data.
 *
 * The viewport client is the point of contact between the toolkit and anything related to manipulating the viewport.
 * As a general guideline on using this class, any interaction with viewport by external classes should be handled by the viewport client. If the viewport client needs any information
 * from external classes it should request it through a delegate.
 *
 * This class is meant to work on its own but it relies on delegates to get which components should be hidden for views A and B, see GetHiddenComponentsForView.
 * There is also functionality to manipulate components with UE's standard transform gizmo and delegates called when the camera moves and stops.
 * The viewport client also controls the active navigation mode by means of locking and unlocking the navigation
 */
class FMetaHumanEditorViewportClient
	: public FEditorViewportClient
	, public TSharedFromThis<FMetaHumanEditorViewportClient>
{
public:
	UE_API FMetaHumanEditorViewportClient(FPreviewScene* InPreviewScene, class UMetaHumanViewportSettings* InViewportSettings = nullptr);
	UE_API virtual ~FMetaHumanEditorViewportClient();

	//~Begin FEditorViewportClient interface
	UE_API virtual void Tick(float InDeltaSeconds) override;
	UE_API virtual void ProcessClick(FSceneView& InView, HHitProxy* InHitProxy, FKey InKey, EInputEvent InEvent, uint32 InHitX, uint32 InHitY) override;
	UE_API virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
	UE_API virtual void SetWidgetMode(UE::Widget::EWidgetMode InWidgetMode) override;
	UE_API virtual FVector GetWidgetLocation() const override;
	UE_API virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bInIsDraggingWidget, bool bInNudge) override;
	UE_API virtual void TrackingStopped() override;
	UE_API virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	UE_API virtual void PerspectiveCameraMoved() override;
	UE_API virtual void EndCameraMovement() override;
	UE_API virtual void UpdateMouseDelta() override;
	UE_API virtual void SetCameraSpeedSettings(const FEditorViewportCameraSpeedSettings& InCameraSpeedSettings) override;
	//~End FEditorViewportClient interface

	//~Begin FGCObject interface */
	UE_API virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	UE_API virtual FString GetReferencerName() const override;
	//~End FGCObject interface */

public:

	/** A delegate used to query all primitive components that are in the preview scene */
	DECLARE_DELEGATE_RetVal(TArray<class UPrimitiveComponent*>, FOnGetAllPrimitiveComponents)
	FOnGetAllPrimitiveComponents OnGetAllPrimitiveComponentsDelegate;

	/** A delegate used get the primitive component instance of a given primitive component. The instance is what is displayed */
	DECLARE_DELEGATE_RetVal_OneParam(class UPrimitiveComponent*, FOnGetPrimitiveComponentInstance, class UPrimitiveComponent* InPrimitiveComponent)
	FOnGetPrimitiveComponentInstance OnGetPrimitiveComponentInstanceDelegate;

	/** A delegate used to query which components are selected so they can be highlighted in the viewport */
	DECLARE_DELEGATE_RetVal(TArray<class UPrimitiveComponent*>, FOnGetSelectedPrimitiveComponents)
	FOnGetSelectedPrimitiveComponents OnGetSelectedPrimitivesComponentsDelegate;

	/** A delegate used to query which pose is currently selected in the tree view */
	DECLARE_DELEGATE_RetVal(EIdentityPoseType, FOnGetSelectedPoseType)
	FOnGetSelectedPoseType OnGetSelectedPoseTypeDelegate;

	/** Delegated called when a click is detected in a component */
	DECLARE_DELEGATE_OneParam(FOnPrimitiveComponentClicked, const class UPrimitiveComponent*)
	FOnPrimitiveComponentClicked OnPrimitiveComponentClickedDelegate;

	/**
	 * A delegate called to query if the navigation should really be unlocked if there is no visible footage component in the current view mode.
	 * UpdateABVisibility will determine if the navigation should be locked or unlocked based on which components are present in the current view mode.
	 * If it determines that no footage component is visible it defaults to unlock the navigation, going back to 3D mode, however, this delegate
	 * can be used to keep the navigation locked if it returns false.
	 */
	DECLARE_DELEGATE_RetVal(bool, FOnShouldUnlockNavigation)
	FOnShouldUnlockNavigation OnShouldUnlockNavigationDelegate;

	/** Called every time the camera moves */
	FSimpleMulticastDelegate OnCameraMovedDelegate;

	/** Called when the camera stops moving */
	FSimpleMulticastDelegate OnCameraStoppedDelegate;

	/** Triggered when footage depth data is changed. */
	DECLARE_DELEGATE_TwoParams(FOnUpdateDepthData, float, float)
	FOnUpdateDepthData OnUpdateFootageDepthDataDelegate;

	/** Triggered when mesh depth data is changed. */
	FOnUpdateDepthData OnUpdateMeshDepthDataDelegate;

	/** Triggered when the visibility of the depth map is changed */
	DECLARE_DELEGATE_OneParam(FOnUpdateDepthMapVisibility, bool)
	FOnUpdateDepthMapVisibility OnUpdateDepthMapVisibilityDelegate;

public:

	/** Can be overridden to determine which components are visible for a given view mode */
	virtual TArray<class UPrimitiveComponent*> GetHiddenComponentsForView(EABImageViewMode InViewMode) const { return {}; }

	/** Focus the viewport on the selected components. Uses OnGetSelectedPrimitivesComponentsDelegate to determine the bounding box to focus on */
	UE_API virtual void FocusViewportOnSelection();

	/**
	 * Function that should be called anytime the visibility state of a component in the viewport changes.
	 * This function will hide components depending on the active view mode and will manage the scene capture
	 * components responsible for capturing the views A and B while in AB Split or AB Wipe modes.
	 */
	UE_API virtual void UpdateABVisibility(bool bInSetViewpoint = true);

	/** Return true if the EV100 value can be changed */
	UE_API virtual bool CanChangeEV100(EABImageViewMode InViewMode) const;

	/** Returns true if the view mode can be changed */
	UE_API virtual bool CanChangeViewMode(EABImageViewMode InViewMode) const;

	/** Returns which of InAllComponents is the active, but not necessarily visible, footage component */
	UE_API virtual class UMetaHumanFootageComponent* GetActiveFootageComponent(const TArray<UPrimitiveComponent*>& InAllComponents) const;

	/** Returns true if the view point is defined by the capture data (true for cases where you have footage, and thus a camera, but false for mesh capture data) */
	UE_API virtual bool GetSetViewpoint() const;

	/** Updates the scene capture components based on the active view mode with the option to clear the hidden components */
	UE_API void UpdateSceneCaptureComponents(bool bInClearHiddenComponents = false);

	/** Sets the viewport widget we are going to control and creates the scene capture components to handle views A and B. */
	UE_API void SetEditorViewportWidget(TSharedRef<class SMetaHumanEditorViewport> InEditorViewportWidget);

	/** Returns if the given EViewModeIndex is enable for a particular EABImageViewMode */
	UE_API bool IsViewModeIndexEnabled(EABImageViewMode InViewMode, EViewModeIndex InViewModeIndex, bool) const;

	/** Sets the view mode index for view A or view B */
	UE_API void SetViewModeIndex(EABImageViewMode InViewMode, EViewModeIndex InViewModeIndex, bool bInNotify);

	/** Returns the EV100 value for view A or view B */
	UE_API float GetEV100(EABImageViewMode InViewMode) const;

	/** Sets the exposure for view A or view B. The value comes first to allow binding directly to the spinbox Value function */
	UE_API void SetEV100(float InValue, EABImageViewMode InViewMode, bool bInNotify);

	/** Returns the default post process settings to be used with this viewport */
	static UE_API FPostProcessSettings GetDefaultPostProcessSettings();

	/** Returns the post process settings for the current view if single of View A if wipe or dual */
	UE_API const FPostProcessSettings& GetPostProcessSettingsForCurrentView() const;

	/** Sets the depth mesh component used to display depth data as a 3D mesh */
	UE_API void SetDepthMeshComponent(class UMetaHumanDepthMeshComponent* InDepthMeshComponent);

	/** Updates footage depth data. */
	UE_API void SetFootageDepthData(const FMetaHumanViewportClientDepthData& InDepthData);

	/** Returns footage depth data. */
	UE_API FMetaHumanViewportClientDepthData GetFootageDepthData() const;

	/** Updates mesh depth data. */
	UE_API void SetMeshDepthData(const FMetaHumanViewportClientDepthData& InDepthData);

	/** Returns mesh depth data. */
	UE_API FMetaHumanViewportClientDepthData GetMeshDepthData() const;

	/** Returns the view mode index for view A or view B */
	UE_API EViewModeIndex GetViewModeIndexForABViewMode(EABImageViewMode InViewMode) const;

	/** Returns the current active AB view mode */
	UE_API EABImageViewMode GetABViewMode() const;

	/** Lock the navigation by changing it to 2D navigation mode */
	UE_API void SetNavigationLocked(bool bIsLocked);

	/** Returns whether or not the navigation is locked for the active AB view mode */
	UE_API bool IsNavigationLocked() const;

	/** Returns whether or not the camera is moving */
	UE_API bool IsCameraMoving() const;

	/** Sets the shape annotation with tracking data to be drawn in the viewport as an overlay */
	UE_API void SetCurveDataController(const TSharedPtr<class FMetaHumanCurveDataController> InCurveDataController);

	/** Sets the size of the tracker image. This is used to place the contour data on screen */
	UE_API void SetTrackerImageSize(const FIntPoint& InTrackerImageSize);

	/** Sets whether or not points and curves can be edited by means of user interaction */
	UE_API void SetEditCurvesAndPointsEnabled(bool bInCanEdit);

	/** Updates the tracker image viewer by resetting its state */
	UE_API void RefreshTrackerImageViewer();

	/** Calls a AB wipe reset on SABImage */
	UE_API void ResetABWipePostion();

	/** Store the camera state in the viewport settings so it can be serialized and restored later */
	UE_API void StoreCameraStateInViewportSettings();

	/** Sets the new active AB view mode */
	UE_API void SetABViewMode(EABImageViewMode InViewMode);

	/** Trigger viewport settings changed delegate to broadcast */
	UE_API void NotifyViewportSettingsChanged() const;

	UE_API bool IsShowingSingleView() const;
	UE_API bool IsShowingDualView() const;
	UE_API bool IsShowingWipeView() const;

	UE_API bool IsShowingViewA() const;
	UE_API bool IsShowingViewB() const;

	UE_API void ToggleABViews();
	UE_API void ToggleShowCurves(EABImageViewMode InViewMode);
	UE_API void ToggleShowControlVertices(EABImageViewMode InViewMode);

	UE_API virtual bool CanToggleShowCurves(EABImageViewMode InViewMode) const;
	UE_API virtual bool CanToggleShowControlVertices(EABImageViewMode InViewMode) const;

	/** Returns the state of the Curves toggle */
	UE_API bool IsShowingCurves(EABImageViewMode InViewMode) const;

	/** Returns the state of the Control Vertices toggle */
	UE_API bool IsShowingControlVertices(EABImageViewMode InViewMode) const;

	/** Returns whether the viewport allows the rendering of the curves */
	UE_API virtual bool ShouldShowCurves(EABImageViewMode InViewMode) const;
	/** Returns whether the viewport allows the rendering of the control points */
	UE_API virtual bool ShouldShowControlVertices(EABImageViewMode InViewMode) const;

	UE_API virtual bool IsFootageVisible(EABImageViewMode InViewMode) const;

	UE_API bool IsRigVisible(EABImageViewMode InViewMode) const;
	UE_API bool IsDepthMeshVisible(EABImageViewMode InViewMode) const;
	UE_API bool IsShowingUndistorted(EABImageViewMode InViewMode) const;

	UE_API void ToggleRigVisibility(EABImageViewMode InViewMode);
	UE_API void ToggleFootageVisibility(EABImageViewMode InViewMode);
	UE_API void ToggleDepthMeshVisible(EABImageViewMode InViewMode);
	UE_API void ToggleDistortion(EABImageViewMode InViewMode);

	/** A default CanExecuteAction function used when mapping actions in with the FABCommandList  */
	bool CanExecuteAction(EABImageViewMode InViewMode) const
	{
		return true;
	}

	/** Returns the size of the STrackerImageViewer widget */
	UE_API FVector2D GetWidgetSize() const;

	/** Returns image coordinates for specified screen position */
	UE_API FVector2D GetPointPositionOnImage(const FVector2D& InScreenPosition) const;

	/** Set a text overlay in the STrackerImageViewer widget */
	UE_API void SetOverlay(const FText& InOverlay) const;

protected:

	/** Map holding the state of each view */
	TMap<EABImageViewMode, TObjectPtr<class UMetaHumanSceneCaptureComponent2D>> ABSceneCaptureComponents;

	/** A post process component used to control exactly how we display the scene in the viewport */
	TObjectPtr<class UPostProcessComponent> PostProcessComponent;

	/** Current depth data setup for footage. */
	FMetaHumanViewportClientDepthData DepthDataFootage { 10.0f, 55.0f, 2.0f, 200.0f };

	/** Current depth data setup for mesh. */
	FMetaHumanViewportClientDepthData DepthDataMesh { 10.0f, 55.0f, 2.0f, 200.0f };

	/** A reference to the depth mesh component */
	TWeakObjectPtr<class UMetaHumanDepthMeshComponent> DepthMeshComponent;

	/** A reference to the viewport settings being used to store the state of this viewport */
	TObjectPtr<class UMetaHumanViewportSettings> ViewportSettings;

private:

	/** Returns a reference to the SMetaHumanEditorViewport we are controlling */
	UE_API TSharedRef<class SMetaHumanEditorViewport> GetMetaHumanEditorViewport() const;

	/** Returns the STrackerImage widget we are controlling. The widget lives in SMetaHumanEditorViewport */
	UE_API TSharedRef<SMetaHumanOverlayWidget<STrackerImageViewer>> GetTrackerImageViewer() const;

	/** Uses OnGetSelectedPrimitivesComponentsDelegate to determine which components are selected */
	UE_API TArray<class UPrimitiveComponent*> GetSelectedPrimitiveComponents() const;

	/** Calculates the bounding box of given list of primitive components  */
	UE_API FBox GetComponentsBoundingBox(const TArray<class UPrimitiveComponent*>& InComponents) const;

	/** Returns the list of all primitive components in the viewport and a map of which components are marked as hidden for view A and B */
	UE_API void GetAllComponentsAndComponentsHiddenForView(TArray<class UPrimitiveComponent*>& OutAllComponents, TMap<EABImageViewMode, TArray<class UPrimitiveComponent*>>& OutHiddenComponentsForView) const;

	/** Determine if there is any footage components visible in the viewport */
	UE_API bool IsAnyFootageComponentVisible(const TArray<class UPrimitiveComponent*>& InAllComponents, const TMap<EABImageViewMode, TArray<class UPrimitiveComponent*>>& InHiddenComponentsForView) const;

	/** Update the camera viewport size based on the available footage image size*/
	UE_API void UpdateCameraViewportFromFootage(const TArray<UPrimitiveComponent*>& InAllComponents, bool bInIsAnyFootageComponentVisible, bool bInSetViewpoint);

private:

	/** The current gizmo widget mode */
	UE::Widget::EWidgetMode WidgetMode;

	/** The initial pivot location when the user starts interacting with gizmos in the viewport */
	FVector InitialPivotLocation;

	// True if we are manipulating a component through a gizmo
	uint8 bIsManipulating : 1;

	// The transaction used to record modifications done using the gizmos in the viewport
	TUniquePtr<class FScopedTransaction> ScopedTransaction;
};

#undef UE_API
