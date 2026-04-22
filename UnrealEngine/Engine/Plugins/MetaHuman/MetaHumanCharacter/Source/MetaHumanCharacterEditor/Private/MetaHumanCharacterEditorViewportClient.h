// Copyright Epic Games, Inc.All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"
#include "UObject/WeakInterfacePtr.h"

class UMetaHumanCharacter;
class IMetaHumanCharacterEditorActorInterface;
enum class EMetaHumanCharacterCameraFrame : uint8;
enum class EMetaHumanCharacterRenderingQuality : uint8;

class FMetaHumanCharacterViewportClient : public FEditorViewportClient
{
public:
	FMetaHumanCharacterViewportClient(
		FEditorModeTools* InModeTools, 
		FPreviewScene* InPreviewScene, 
		TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> InEditingActor,
		TWeakObjectPtr<UMetaHumanCharacter> InCharacter);

	//~Begin FEditorViewportClient interface
	virtual void Tick(float InDeltaSeconds) override;
	virtual bool InputAxis(const FInputKeyEventArgs& InEventArgs) override;
	virtual void OverridePostProcessSettings(FSceneView& View) override;
	virtual void SetupViewForRendering(FSceneViewFamily& InViewFamily, FSceneView& InOutView) override;
	virtual bool InputKey(const FInputKeyEventArgs& InEventArgs) override;
	virtual bool ShouldOrbitCamera() const override;
	virtual void Draw(FViewport* InViewport, FCanvas* InCanvas) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override;
	virtual void ProcessAccumulatedPointerInput(FViewport* InViewport) override;
	//~End FEditorViewportClientInterface

	TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> WeakCharacterActor;
	TWeakObjectPtr<UMetaHumanCharacter> WeakCharacter;

	void FocusOnSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate);
	void SetAutoFocusToSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate);
	void RescheduleFocus();

	void ChangeRenderQuality(EMetaHumanCharacterRenderingQuality InRenderQuality);

	void SetViewportWidget(const TWeakPtr<SEditorViewport>& InEditorViewportWidget);

	void ClearShortcuts();
	void SetShortcuts(const TArray<TPair<FText, FText>>& InShortcuts);

	void ShowFaceBonesOnCharacter(const bool InDrawFaceBones);
	
	void ShowBodyBonesOnCharacter(const bool InDrawBodyBones);

	void DrawDebugBones(const USkeletalMeshComponent* MetaHumanSkeletalMeshComponent, bool bIsFaceComponent, FPrimitiveDrawInterface* PDI);

	void ShowFaceNormalsOnCharacter(const bool InShowFaceNormals);
	void ShowBodyNormalsOnCharacter(const bool InShowBodyNormals);

	void ShowFaceTangentsOnCharacter(const bool InShowFaceTangents);
	void ShowBodyTangentsOnCharacter(const bool InShowBodyTangents);

	void ShowFaceBinormalsOnCharacter(const bool InShowFaceBinormals);
	void ShowBodyBinormalsOnCharacter(const bool InShowBodyBinormals);
private:

	void FocusOnFace(float InDistanceScale, const FVector& InOffset, bool bInInstant);

	void FocusOnBody(float InDistanceScale, const FVector& InOffset, bool bInInstant);

	struct FPostProcessSettings PostProcessSettings;

	void SetTransmissionForAllLights(bool bTransmissionEnabled);

	struct FDrawInfoOptions
	{
		FIntPoint TopCenter;
		bool bTitleLeft = true;
		int32 Padding = 2;
	};

	void DrawInfos(FCanvas* InCanvas, const FText& Title, const TArray<TPair<FText, FText>>& Infos, const FDrawInfoOptions& InDrawInfoOptions) const;

private:
	/** Flag whether an initial viewport camera framing has been performed. */
	bool bIsViewportFramed;

	/** Stores the last camera yaw value, used to rotate the light rig with the camera */
	float LastCameraYaw = 0.0f;

	/** Flag for showing Face bones in Draw function*/
	bool bDrawFaceBones = false;

	/** Flag for showing Body bones in Draw function*/
	bool bDrawBodyBones = false;

	/** Camera framing for auto framing mode. */
	EMetaHumanCharacterCameraFrame AutoSelectedFrame;

	/** Last selected camera framing in viewport. */
	EMetaHumanCharacterCameraFrame LastSelectedFrame;

	/** Viewport message. */
	FText ViewportMessage;

	/** Shortcuts */
	TArray<TPair<FText, FText>> Shortcuts;

	/** Previous mouse position */
	TOptional<FInt32Point> PreviousMousePosition;
	TOptional<FInt32Point> NextMousePosition;

	/** Bone drawing information*/
	TArray<FTransform> FaceBonesWorldTransforms;
	TArray<FTransform> BodyBonesWorldTransforms;

	TArray<FLinearColor> FaceBoneColors;
	TArray<FLinearColor> BodyBoneColors;

	TArray<TRefCountPtr<HHitProxy>> HitProxies;
};
