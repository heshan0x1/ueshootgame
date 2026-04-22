// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimationEditorTypes.h"
#include "Delegates/IDelegateInstance.h"
#include "Misc/Guid.h"
#include "InputCoreTypes.h"
#include "HitProxies.h"
#include "UnrealWidgetFwd.h"
#include "EditorViewportClient.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "IPersonaPreviewScene.h"
#include "Preferences/PersonaOptions.h"
#include "SkeletalDebugRendering.h"

#define UE_API PERSONA_API

class FCanvas;
class UPersonaOptions;
class USkeletalMeshSocket;
struct FCompactHeapPose;
struct FSkelMeshRenderSection;

DECLARE_DELEGATE_OneParam(FOnBoneSizeSet, float);
DECLARE_DELEGATE_RetVal(float, FOnGetBoneSize)

/////////////////////////////////////////////////////////////////////////
// FAnimationViewportClient

class FAnimationViewportClient : public FEditorViewportClient, public TSharedFromThis<FAnimationViewportClient>
{
protected:

	/** Function to display bone names*/
	UE_API void ShowBoneNames(FCanvas* Canvas, FSceneView* View, UDebugSkelMeshComponent* MeshComponent);

	/** Function to display transform attribute names*/
	UE_API void ShowAttributeNames(FCanvas* Canvas, FSceneView* View, UDebugSkelMeshComponent* MeshComponent) const;

	/** Function to display debug lines generated from skeletal controls in animBP mode */
	UE_API void DrawNodeDebugLines(TArray<FText>& Lines, FCanvas* Canvas, FSceneView* View);

public:
	UE_API FAnimationViewportClient(const TSharedRef<class IPersonaPreviewScene>& InPreviewScene, const TSharedRef<class SAnimationEditorViewport>& InAnimationEditorViewport, const TSharedRef<class FAssetEditorToolkit>& InAssetEditorToolkit, int32 InViewportIndex, bool bInShowStats);
	UE_API virtual ~FAnimationViewportClient() override;

	UE_API void Initialize();

	// FEditorViewportClient interface
	UE_API virtual void Tick(float DeltaSeconds) override;
	UE_API virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override;
	UE_API virtual void DrawCanvas( FViewport& InViewport, FSceneView& View, FCanvas& Canvas ) override;
	UE_API virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	UE_API virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
	UE_API virtual void SetCameraSpeedSettings(const FEditorViewportCameraSpeedSettings& InCameraSpeedSettings) override;

	// Sets what bones are drawn by DrawMeshBones and ShowBoneNames
	UE_API virtual void UpdateBonesToDraw();
	
//	virtual void ProcessClick(class FSceneView& View, class HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
//	virtual bool InputWidgetDelta( FViewport* Viewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale ) override;
	UE_API virtual void TrackingStarted( const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge ) override;
	UE_API virtual void TrackingStopped() override;
//	virtual UE::Widget::EWidgetMode GetWidgetMode() const override;
//	virtual void SetWidgetMode(UE::Widget::EWidgetMode InWidgetMode) override;
//	virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const override;
	UE_API virtual FVector GetWidgetLocation() const override;
	UE_API virtual FMatrix GetWidgetCoordSystem() const override;
	UE_API virtual ECoordSystem GetWidgetCoordSystemSpace() const override;
	UE_API virtual void SetWidgetCoordSystemSpace(ECoordSystem NewCoordSystem) override;
	UE_API virtual void SetViewMode(EViewModeIndex InViewModeIndex) override;
	UE_API virtual void SetViewportType(ELevelViewportType InViewportType) override;
	UE_API virtual void RotateViewportType() override;
	UE_API virtual bool CanCycleWidgetMode() const override;
	UE_API virtual void SetupViewForRendering( FSceneViewFamily& ViewFamily, FSceneView& View ) override;
	UE_API virtual void HandleToggleShowFlag(FEngineShowFlags::EShowFlag EngineShowFlagIndex) override;
	UE_API virtual FMatrix CalcViewRotationMatrix(const FRotator& InViewRotation) const override;
	// End of FEditorViewportClient interface

	/** Draw call to render UV overlay */
	UE_API void DrawUVsForMesh(FViewport* InViewport, FCanvas* InCanvas, int32 InTextYPos, UDebugSkelMeshComponent* MeshComponent);

	/** Set the camera follow mode */
	UE_API void SetCameraFollowMode(EAnimationViewportCameraFollowMode Mode, FName InBoneName = NAME_None);

	/** Called when viewport focuses on a selection */
	UE_API void OnFocusViewportToSelection();

	/** Get the camera follow mode */
	UE_API EAnimationViewportCameraFollowMode GetCameraFollowMode() const;

	/** Get the bone name to use when CameraFollowMode is EAnimationViewportCameraFollowMode::Bone */
	UE_API FName GetCameraFollowBoneName() const;

	/** Jump to the meshes default camera */
	UE_API void JumpToDefaultCamera();

	/** Save current camera as default for mesh */
	UE_API void SaveCameraAsDefault();

	/** Check whether we can save this camera as default */
	UE_API bool CanSaveCameraAsDefault() const;

	/** Clear any default camera for mesh */
	UE_API void ClearDefaultCamera();

	/** Returns whether we have a default camera set for this mesh */
	UE_API bool HasDefaultCameraSet() const;

	/** Handle the skeletal mesh mesh component being used for preview changing */
	UE_API void HandleSkeletalMeshChanged(class USkeletalMesh* OldSkeletalMesh, class USkeletalMesh* NewSkeletalMesh);

	/** Handle a change in the skeletal mesh mesh component being used for preview changing */
	UE_API void HandleOnMeshChanged();

	/** Handle a change in the skeletal mesh phusics component being used for preview changing */
	UE_API void HandleOnSkelMeshPhysicsCreated();

	/** Function to display bone names*/
	UE_API void ShowBoneNames(FViewport* Viewport, FCanvas* Canvas, UDebugSkelMeshComponent* MeshComponent);

	/** Function to enable/disable floor auto align */
	UE_API void OnToggleAutoAlignFloor();

	/** Function to check whether floor is auto align or not */
	UE_API bool IsAutoAlignFloor() const;

	/** Function to mute/unmute audio in the viewport */
	UE_API void OnToggleMuteAudio();

	/** Function to check whether audio is muted or not */
	UE_API bool IsAudioMuted() const;

	/** Set whether to use audio attenuation */
	UE_API void OnToggleUseAudioAttenuation();

	/** Check whether we are using audio attenuation */
	UE_API bool IsUsingAudioAttenuation() const;

	/** Function to set background color */
	UE_API void SetBackgroundColor(FLinearColor InColor);

	/** Function to get current brightness value */ 
	UE_API float GetBrightnessValue() const;

	/** Function to set brightness value */
	UE_API void SetBrightnessValue(float Value);

	/** Function to set Local axes mode for the ELocalAxesType */
	UE_API void SetLocalAxesMode(ELocalAxesMode::Type AxesMode);

	/** Local axes mode checking function for the ELocalAxesType */
	UE_API bool IsLocalAxesModeSet(ELocalAxesMode::Type AxesMode) const;

	/** Get the Bone local axis mode */
	UE_API ELocalAxesMode::Type GetLocalAxesMode() const;

	/** Access Bone Draw size config option*/
	UE_API void SetBoneDrawSize(const float InBoneDrawSize);
	UE_API float GetBoneDrawSize() const;

	/** Access CustomAnimationSpeed config option*/
	UE_DEPRECATED(5.6, "Animation Speed is now handled by FAnimationEditorPreviewScene. Please use FAnimationEditorPreviewScene::SetCustomAnimationSpeed instead.")
	UE_API void SetCustomAnimationSpeed(const float InCustomAnimationSpeed);
	UE_DEPRECATED(5.6, "Animation Speed is now handled by FAnimationEditorPreviewScene. Please use FAnimationEditorPreviewScene::GetCustomAnimationSpeed instead.")
	UE_API float GetCustomAnimationSpeed() const;

	/** Function to set Bone Draw  mode for the EBoneDrawType */
	UE_API void SetBoneDrawMode(EBoneDrawMode::Type AxesMode);

	/** Bone Draw  mode checking function for the EBoneDrawType */
	UE_API bool IsBoneDrawModeSet(EBoneDrawMode::Type AxesMode) const;

	/** Get the Bone local axis mode */
	UE_API EBoneDrawMode::Type GetBoneDrawMode() const;
	
	/** Returns the desired target of the camera */
	UE_API FSphere GetCameraTarget();

	/** Sets up the viewports camera (look-at etc) based on the current preview target*/
	UE_API void UpdateCameraSetup();

	/* Places the viewport camera at a good location to view the supplied sphere */
	UE_API void FocusViewportOnSphere( FSphere& Sphere, bool bInstant = true );

	/* Places the viewport camera at a good location to view the preview target */
	UE_API void FocusViewportOnPreviewMesh(bool bUseCustomCamera);

	/** Callback for toggling the normals show flag. */
	UE_API void ToggleCPUSkinning();

	/** Callback for checking the normals show flag. */
	UE_API bool IsSetCPUSkinningChecked() const;

	/** Toggles whether to lock the camera's rotation to a specified bone's orientation */
	UE_API void ToggleRotateCameraToFollowBone();

	/** Whether or not to lock the camera's rotation to a specified bone's orientation */
	UE_API bool GetShouldRotateCameraToFollowBone() const;

	/** Callback for toggling the normals show flag. */
	UE_API void ToggleShowNormals();

	/** Callback for checking the normals show flag. */
	UE_API bool IsSetShowNormalsChecked() const;

	/** Callback for toggling the tangents show flag. */
	UE_API void ToggleShowTangents();

	/** Callback for checking the tangents show flag. */
	UE_API bool IsSetShowTangentsChecked() const;

	/** Callback for toggling the binormals show flag. */
	UE_API void ToggleShowBinormals();

	/** Callback for checking the binormals show flag. */
	UE_API bool IsSetShowBinormalsChecked() const;

	/** Callback for toggling UV drawing in the viewport */
	UE_API void SetDrawUVOverlay(bool bInDrawUVs);

	/** Callback for checking whether the UV drawing is switched on. */
	UE_API bool IsSetDrawUVOverlayChecked() const;

	/** Returns the UV Channel that will be drawn when Draw UV Overlay is turned on */
	int32 GetUVChannelToDraw() const { return UVChannelToDraw; }

	/** Sets the UV Channel that will be drawn when Draw UV Overlay is turned on */
	void SetUVChannelToDraw(int32 UVChannel) { UVChannelToDraw = UVChannel; }

	/* Returns the floor height offset */	
	UE_API float GetFloorOffset() const;

	/* Sets the floor height offset, saves it to config and invalidates the viewport so it shows up immediately */
	UE_API void SetFloorOffset(float NewValue);

	/** Function to set mesh stat drawing state */
	UE_API void OnSetShowMeshStats(int32 ShowMode);
	/** Whether or not mesh stats are being displayed */
	UE_API bool IsShowingMeshStats() const;
	/** Whether or not selected node stats are being displayed */
	UE_API bool IsShowingSelectedNodeStats() const;
	/** Whether detailed mesh stats are being displayed or basic mesh stats */
	UE_API bool IsDetailedMeshStats() const;

	UE_API int32 GetShowMeshStats() const;

	/** Set the playback speed mode */
	UE_DEPRECATED(5.6, "Animation Speed is now handled by FAnimationEditorPreviewScene. Please use FAnimationEditorPreviewScene::SetAnimationPlaybackSpeedMode instead.")
	UE_API void SetPlaybackSpeedMode(EAnimationPlaybackSpeeds::Type InMode);

	/** Get the playback speed mode */
	UE_DEPRECATED(5.6, "Animation Speed is now handled by FAnimationEditorPreviewScene. Please use FAnimationEditorPreviewScene::GetAnimationPlaybackSpeedMode instead.")
	UE_API EAnimationPlaybackSpeeds::Type GetPlaybackSpeedMode() const;

	/** Get the preview scene we are viewing */
	TSharedRef<class IPersonaPreviewScene> GetPreviewScene() const { return PreviewScenePtr.ToSharedRef(); }

	/** Get the asset editor we are embedded in */
	TSharedRef<class FAssetEditorToolkit> GetAssetEditorToolkit() const { return AssetEditorToolkitPtr.Pin().ToSharedRef(); }

	/* Handle error checking for additive base pose */
	UE_API bool ShouldDisplayAdditiveScaleErrorMessage() const;

	/** Draws Mesh Sockets in foreground - bUseSkeletonSocketColor = true for grey (skeleton), false for red (mesh) **/
	static UE_API void DrawSockets(const UDebugSkelMeshComponent* InPreviewMeshComponent, TArray<USkeletalMeshSocket*>& InSockets, FSelectedSocketInfo InSelectedSocket, FPrimitiveDrawInterface* PDI, bool bUseSkeletonSocketColor);

	/** Draws Gizmo for the Transform in foreground **/
	static UE_API void RenderGizmo(const FTransform& Transform, FPrimitiveDrawInterface* PDI);

	/** Function to display warning and info text on the viewport when outside of animBP mode */
	UE_API FText GetDisplayInfo( bool bDisplayAllInfo ) const;

	/** Get the viewport index (0-3) for this client */
	int32 GetViewportIndex() const { return ViewportIndex; }

	/** Get the persona mode manager */
	UE_DEPRECATED(5.1, "Use the UPersonaEditorModeManagerContext object stored in the editor mode tools' context store instead.")
	UE_API class IPersonaEditorModeManager* GetPersonaModeManager() const;

private:
	/**
	 * Updates the audio listener for this viewport 
	 *
	 * @param View	The scene view to use when calculate the listener position
	 */
	UE_API void UpdateAudioListener(const FSceneView& View);

public:

	/** persona config options **/
	UPersonaOptions* ConfigOption;

	/** allow client code to store/serialize bone size if desired */
	FOnBoneSizeSet OnSetBoneSize;
	FOnGetBoneSize OnGetBoneSize;

private:
	/** Shared pointer back to the preview scene we are viewing 
	* Workaround fix for FORT-495476, UE-159733, UE-160424, UE-145060
	* We hold a shared because if the PreviewScene gets destroyed before we reach
	* this class destructor, we can not unregister the callbacks from this class
	* and we get crashes when any of the callbacks is triggered afterwards
	*/
	TSharedPtr<class IPersonaPreviewScene> PreviewScenePtr;

	/** Weak pointer back to asset editor we are embedded in */
	TWeakPtr<class FAssetEditorToolkit> AssetEditorToolkitPtr;

	// Current widget mode
	UE::Widget::EWidgetMode WidgetMode;

	/** The current camera follow mode */
	EAnimationViewportCameraFollowMode CameraFollowMode;

	/** The bone we will follow when in EAnimationViewportCameraFollowMode::Bone */
	FName CameraFollowBoneName;

	/** Should we auto align floor to mesh bounds */
	bool bAutoAlignFloor;

	/** Whether to lock the camera's rotation to a specified bone's orientation */
	bool bRotateCameraToFollowBone;

	/** User selected color using color picker */
	FLinearColor SelectedHSVColor;

	/** Flag for displaying the UV data in the viewport */
	bool bDrawUVs;

	/** Which UV channel to draw */
	int32 UVChannelToDraw;

	enum GridParam
	{
		MinCellCount = 64,
		MinGridSize = 2,
		MaxGridSize	= 50,
	};

	/** Focus on the preview component the next time we draw the viewport */
	bool bFocusOnDraw;
	bool bFocusUsingCustomCamera;

	/** Handle additive anim scale validation */
	mutable bool bDoesAdditiveRefPoseHaveZeroScale;
	mutable FGuid RefPoseGuid;

	/** Screen size cached when we draw */
	float CachedScreenSize;

	/* Member use to unregister OnPhysicsCreatedDelegate */
	FDelegateHandle OnPhysicsCreatedDelegateHandle;

	/* Member use to unregister OnMeshChanged for our preview skeletal mesh */
	FDelegateHandle OnMeshChangedDelegateHandle;

	/* Bit field indexed on bone index that stores what bones are visible in the viewport, updated with UpdateBonesToDraw */
	TBitArray<> BonesToDraw;

	FDelegateHandle OnSelectedBonesChangedHandle;

private:

	UE_API void SetCameraTargetLocation(const FSphere &BoundSphere, float DeltaSeconds);

	/** Draws Mesh Bones in foreground **/
	UE_API void DrawMeshBones(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draws the given array of transforms as bones */
	UE_API void DrawBonesFromTransforms(TArray<FTransform>& Transforms, UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI, FLinearColor BoneColour, FLinearColor RootBoneColour) const;
	/** Draws Bones for a compact pose */
	UE_API void DrawBonesFromCompactPose(const FCompactHeapPose& Pose, UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI, const FLinearColor& DrawColor) const;
	/** Draws Bones for uncompressed animation **/
	UE_API void DrawMeshBonesUncompressedAnimation(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draw Bones for non retargeted animation. */
	UE_API void DrawMeshBonesNonRetargetedAnimation(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draws Bones for Additive Base Pose */
	UE_API void DrawMeshBonesAdditiveBasePose(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draw Bones for non retargeted animation. */
	UE_API void DrawMeshBonesSourceRawAnimation(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draw Bones for non retargeted animation. */
	UE_API void DrawMeshBonesBakedAnimation(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI) const;
	/** Draw Bones from skeleton reference pose. */
	UE_API void DrawBonesFromSkeleton(UDebugSkelMeshComponent * MeshComponent, const USkeleton* Skeleton, const TArray<int32>& InSelectedBones, FPrimitiveDrawInterface* PDI) const;
	/** Draws Bones for RequiredBones with WorldTransform **/
	UE_API void DrawBones(
		const FVector& ComponentOrigin,
		const TArray<FBoneIndexType>& RequiredBones,
		const FReferenceSkeleton& RefSkeleton,
		const TArray<FTransform>& WorldTransforms,
		const TArray<int32>& InSelectedBones,
		const TArray<FLinearColor>& BoneColors,
		FPrimitiveDrawInterface* PDI,
		bool bForceDraw,
		bool bAddHitProxy,
		bool bUseMultiColors) const;

	/** Draws active transform attributes */
	UE_API void DrawAttributes(UDebugSkelMeshComponent* MeshComponent, FPrimitiveDrawInterface* PDI) const;

	/** Draws visualization from animation notifies into viewport. */
	UE_API void DrawNotifies(UDebugSkelMeshComponent* MeshComponent, FPrimitiveDrawInterface* PDI) const;

	/* Draws visualization from animation notifies into canvas. */
	UE_API void DrawCanvasNotifies(UDebugSkelMeshComponent* MeshComponent, FCanvas& Canvas, FSceneView& View) const;

	/* Draws visualization from Asset User Data into viewport. */
	UE_API void DrawAssetUserData(FPrimitiveDrawInterface* PDI) const;

	/* Draws visualization from Asset User Data into canvas. */
	UE_API void DrawCanvasAssetUserData(FCanvas& Canvas, FSceneView& View) const;

	/** Draws root motion trajectory */
	UE_API void DrawRootMotionTrajectory(UDebugSkelMeshComponent* MeshComponent, FPrimitiveDrawInterface* PDI) const;

	/** Draws bones from watched poses*/
	UE_API void DrawWatchedPoses(UDebugSkelMeshComponent * MeshComponent, FPrimitiveDrawInterface* PDI);

	/** Get the typed anim preview scene shared ptr*/
	UE_API TSharedPtr<class FAnimationEditorPreviewScene> GetAnimPreviewScenePtr() const;

	/** Get the typed anim preview scene */
	UE_API TSharedRef<class FAnimationEditorPreviewScene> GetAnimPreviewScene() const;

	/** Invalidate this view in response to a preview scene change */
	UE_API void HandleInvalidateViews();

	/** Handle the view being focused from the preview scene */
	UE_API void HandleFocusViews();

	/** Computes a bounding box for the selected section of the preview mesh component.
	    If there is no selected section, returns an empty box. */
	UE_API FBox ComputeBoundingBoxForSelectedEditorSection() const;

	/** Converts all local vertex positions to world positions. */
	UE_API void TransformVertexPositionsToWorld(TArray<FFinalSkinVertex>& LocalVertices) const;

	/** Gets all vertex indices that the given section references. */
	UE_API void GetAllVertexIndicesUsedInSection(const FRawStaticIndexBuffer16or32Interface& IndexBuffer,
										  const FSkelMeshRenderSection& SkelMeshSection,
										  TArray<int32>& OutIndices) const;

	/** Used for camera tracking - store data about the scene pre-tick */
	UE_API void HandlePreviewScenePreTick();

	/** Used for camera tracking - use stored data about the scene post-tick */
	UE_API void HandlePreviewScenePostTick();

private:
	struct FTimecodeDisplayInfo
	{
		FQualifiedFrameTime QualifiedTime;
		FString	  Slate;
	};

	/** @return array of AssetUserData interfaces from editable objects on associated asset toolkit. */
	UE_API TArray<IInterface_AssetUserData*> GetEditedObjectsWithAssetUserData() const;

	/** Size to draw bones in the viewport. Transient setting. */
	float BoneDrawSize=1.0f;
	
	/** Allow mesh stats to be disabled for specific viewport instances */
	bool bShowMeshStats;

	/** Whether we have initially focused on the preview mesh */
	bool bInitiallyFocused;

	/** When orbiting, the base rotation of the camera, allowing orbiting around different axes to Z */
	FQuat OrbitRotation;

	// We allow for replacing this in the underlying client so we cache it here
	FEditorCameraController* CachedDefaultCameraController;

	/** Index (0-3) of this viewport */
	int32 ViewportIndex;

	/** The last location the camera was told to look at */
	FVector LastLookAtLocation;

	// Delegate Handler to allow changing of camera controller
	UE_API void OnCameraControllerChanged();

	/** True when the preview animation should resume playing upon finishing tracking */
	bool bResumeAfterTracking;

	/** Timecode/slate information from current animation sequence. */
	TOptional<FTimecodeDisplayInfo> TimecodeDisplay;
};

#undef UE_API
