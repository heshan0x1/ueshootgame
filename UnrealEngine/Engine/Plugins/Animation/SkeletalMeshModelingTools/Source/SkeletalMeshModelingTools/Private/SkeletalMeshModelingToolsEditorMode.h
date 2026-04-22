// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "SkeletalMeshModelingModeToolExtensions.h"
#include "SkeletalMeshNotifier.h"
#include "Interfaces/ISKMBackedDynaMeshComponentProvider.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Changes/ValueWatcher.h"
#include "TargetInterfaces/MeshTargetInterfaceTypes.h"

#include "SkeletalMeshModelingToolsEditorMode.generated.h"

class FSkeletalMeshModelingToolsEditorModeToolkit;

namespace UE::SkeletalMeshEditorUtils
{
	struct FSkeletalMeshNotifierBindScope;
}

class USkeletalMeshEditingCache;
//class FStylusStateTracker;
class UEdModeInteractiveToolsContext;
class ISkeletalMeshNotifier;
class ISkeletalMeshEditorBinding;
class ISkeletalMeshEditingInterface;
class HHitProxy;
class UDebugSkelMeshComponent;
class ISkeletalMeshEditor;
enum class EToolManagerToolSwitchMode;
class FTabManager;
enum class EToolSide;
enum class EToolShutdownType : uint8;
struct FToolBuilderState;
class USkeletonModifier;

namespace UE
{
	class IInteractiveToolCommandsInterface;
}

class USkeletalMeshModelingToolsEditorMode;
class FSkeletalMeshModelingToolsEditorModeNotifier: public ISkeletalMeshNotifier
{
public:
	FSkeletalMeshModelingToolsEditorModeNotifier(USkeletalMeshModelingToolsEditorMode* InEditorMode);
	virtual void HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) override;
private:
	TWeakObjectPtr<USkeletalMeshModelingToolsEditorMode> EditorMode;
};

class FSkeletalMeshModelingToolsEditorModeBinding: public ISkeletalMeshEditorBinding
{
public:
	FSkeletalMeshModelingToolsEditorModeBinding(USkeletalMeshModelingToolsEditorMode* InEditorMode);

	virtual TSharedPtr<ISkeletalMeshNotifier> GetNotifier() override;
	virtual NameFunction GetNameFunction() override;
	virtual TArray<FName> GetSelectedBones() const override;


private:
	TWeakObjectPtr<USkeletalMeshModelingToolsEditorMode> EditorMode;	
	TSharedPtr<FSkeletalMeshModelingToolsEditorModeNotifier> Notifier;
};



UCLASS()
class USkeletalMeshModelingToolsEditorMode : 
	public UBaseLegacyWidgetEdMode,
	public ISkeletalMeshBackedDynamicMeshComponentProvider 
{
	GENERATED_BODY()
public:
	const static FEditorModeID Id;	

	USkeletalMeshModelingToolsEditorMode();
	explicit USkeletalMeshModelingToolsEditorMode(FVTableHelper& Helper);
	virtual ~USkeletalMeshModelingToolsEditorMode() override;

	// UEdMode overrides
	virtual void Initialize() override;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void CreateToolkit() override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	virtual bool UsesPropertyWidgets() const override { return false; }

	virtual void Tick(FEditorViewportClient* InViewportClient, float InDeltaTime) override;
	virtual bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const override;
	virtual bool UsesToolkits() const override { return true; }
	virtual bool ShouldToolStartBeAllowed(const FString& ToolIdentifier) const override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool UsesTransformWidget() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click) override;

	virtual void PostUndo() override;

	// ISkeletalMeshBackedDynamicMeshComponentProvider
	virtual USkeletalMeshBackedDynamicMeshComponent* GetComponent(UObject* SourceObject) override;
	bool ShouldCommitToSkeletalMeshOnToolCommit() override;
	
	// binding
	TSharedPtr<ISkeletalMeshEditorBinding> GetModeBinding();

	void SetEditorBinding(const TWeakPtr<ISkeletalMeshEditor>& InSkeletalMeshEditor);
	TSharedPtr<ISkeletalMeshEditorBinding> GetEditorBinding();
	TSharedPtr<ISkeletalMeshEditor> GetEditor();
	
	USkeletalMesh* GetSkeletalMesh() const;
	void HandleSkeletalMeshChanged();
	
	bool CanSetEditingLOD();
	void SetEditingLOD(EMeshLODIdentifier EditingLOD);
	EMeshLODIdentifier GetEditingLOD();

	USkeletalMeshEditingCache* GetCurrentEditingCache() const;
	bool HasUnappliedChanges() const;
	void ApplyChanges();
	void DiscardChanges();
	
	enum class EApplyMode: uint8
	{
		ApplyOnToolExit,
		ApplyManually,
	};
	
	EApplyMode GetApplyMode() const;
	void SetApplyMode(EApplyMode InApplyMode);

	
	void HideSkeletonForTool();	
	void ShowSkeletonForTool();
	const TArray<FTransform>& GetComponentSpaceBoneTransforms() const;
	void ToggleBoneManipulation(bool bEnable);
	USkeletonModifier* GetSkeletonReader();
	TArray<FName> GetSelectedBones() const;
	
	FName GetEditingMorphTarget();
	void HandleSetEditingMorphTarget(FName InMorphTarget);

	TArray<FName> GetMorphTargets();

	float GetMorphTargetWeight(FName MorphTarget);
	FName HandleAddMorphTarget(FName InName);
	FName HandleRenameMorphTarget(FName OldName, FName NameName);
	void HandleRemoveMorphTargets(const TArray<FName>& Names);
	TArray<FName> HandleDuplicateMorphTargets(const TArray<FName>& Names);
	
	void HandleSetMorphTargetWeight(FName MorphTarget, float Weight);
	bool GetMorphTargetAutoFill(FName MorphTarget);
	void HandleSetMorphTargetAutoFill(FName MorphTarget, bool bAutoFill, float PreviousOverrideWeight);

	TSharedPtr<FTabManager> GetAssociatedTabManager();
	FSimpleMulticastDelegate& OnInitialized() { return OnInitializedDelegate; }


protected:
	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	void OnToolPostBuild(UInteractiveToolManager* InToolManager, EToolSide InSide, UInteractiveTool* InBuiltTool, UInteractiveToolBuilder* InToolBuilder, const FToolBuilderState& ToolState);
	void OnToolEndedWithStatus(UInteractiveToolManager* Manager, UInteractiveTool* Tool, EToolShutdownType ShutdownType);

private:
	EApplyMode ApplyMode = EApplyMode::ApplyOnToolExit;
	
	TSharedPtr<FSkeletalMeshModelingToolsEditorModeBinding> ModeBinding;

	TWeakPtr<ISkeletalMeshEditor> Editor;
	TWeakPtr<ISkeletalMeshEditorBinding> EditorBinding;
	TWeakObjectPtr<USkeletalMesh> SkeletalMesh;

	TUniquePtr<UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope> EditorBindScope;
	
	UPROPERTY()
	TObjectPtr<USkeletalMeshEditingCache> CurrentEditingCache = nullptr;

	void RecreateEditingCache(EMeshLODIdentifier InLOD);

	TUniquePtr<UE::SkeletalMeshEditorUtils::FSkeletalMeshNotifierBindScope> EditingCacheNotifierBindScope;

	FName EditingMorphTarget = NAME_None;
	
	// Stylus support is currently disabled; this is left in for reference if/when it is brought back
	//TUniquePtr<FStylusStateTracker> StylusStateTracker;

	// we restore previous switch tool behavior when exiting this mode
	EToolManagerToolSwitchMode ToolSwitchModeToRestoreOnExit;

	static ISkeletalMeshEditingInterface* GetSkeletonInterface(UInteractiveTool* InTool);

	UDebugSkelMeshComponent* GetSkelMeshComponent() const;
	void ToggleSkeletalMeshBoneManipulation(bool bEnable);
	bool IsSkeletalMeshBoneManipulationEnabled();

	// A dummy read-only modifier to host the current edited skeleton for RefSkeletonTree UI
	UPROPERTY()
	TObjectPtr<USkeletonModifier> SkeletonReader = nullptr;

	TValueWatcher<int32> CacheChangeCountWatcher;
	TValueWatcher<bool> CacheDynamicMeshSkeletonStatusWatcher;
	TValueWatcher<int32> CacheSkeletonChangeCountWatcher;
	
	TWeakPtr<FSkeletalMeshModelingToolsEditorModeToolkit> TypedToolkit = nullptr;
	
	bool bDeactivateOnPIEStartStateToRestore;

	void RegisterExtensions();
	// Support extension tools having their own hotkey classes
	TMap<FString, FExtensionToolDescription> ExtensionToolToInfo;
	// Note: this will only work when the given tool is active, because we get the tool identifier
	//  out of the manager using GetActiveToolName
	bool TryGetExtensionToolCommandGetter(UInteractiveToolManager* InManager, const UInteractiveTool* InTool, 
		TFunction<const UE::IInteractiveToolCommandsInterface&()>& OutGetter) const;
	// Used to unbind extension tool commands
	TFunction<const UE::IInteractiveToolCommandsInterface& ()> ExtensionToolCommandsGetter;

	FSimpleMulticastDelegate OnInitializedDelegate;
};
