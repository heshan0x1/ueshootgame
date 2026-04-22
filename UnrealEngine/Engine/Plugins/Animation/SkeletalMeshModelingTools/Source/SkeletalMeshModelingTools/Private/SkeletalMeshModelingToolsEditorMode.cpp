// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshModelingToolsEditorMode.h"

#include "AttributeEditorTool.h"
#include "SkeletalMeshModelingToolsEditorModeToolkit.h"
#include "SkeletalMeshModelingToolsCommands.h"
#include "SkeletalMeshGizmoUtils.h"

#include "BaseGizmos/TransformGizmoUtil.h"
#include "EdMode.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Features/IModularFeatures.h"

// Stylus support is currently disabled due to issues with the stylus plugin
// We are leaving the code in this cpp file, defined out, so that it is easier to bring back if/when the stylus plugin is improved.

#include "SkeletalMeshModelingToolsModule.h"
#include "Widgets/Notifications/SNotificationList.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SkeletalMeshModelingToolsEditorMode)
#define ENABLE_STYLUS_SUPPORT 0

#if ENABLE_STYLUS_SUPPORT 
#include "IStylusInputModule.h"
#include "IStylusState.h"
#endif

#include "AnimationEditorViewportClient.h"
#include "ToolTargets/SkeletalMeshComponentToolTarget.h"
#include "Components/SkeletalMeshComponent.h"

#include "ConvertToPolygonsTool.h"
#include "DeformMeshPolygonsTool.h"
#include "DisplaceMeshTool.h"
#include "DynamicMeshSculptTool.h"
#include "EditMeshPolygonsTool.h"
#include "EditorInteractiveGizmoManager.h"
#include "HoleFillTool.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "ISkeletalMeshEditor.h"
#include "LatticeDeformerTool.h"
#include "MeshAttributePaintTool.h"
#include "MeshGroupPaintTool.h"
#include "MeshSpaceDeformerTool.h"
#include "MeshVertexPaintTool.h"
#include "MeshVertexSculptTool.h"
#include "ModelingToolsManagerActions.h"
#include "OffsetMeshTool.h"
#include "PersonaModule.h"
#include "PolygonOnMeshTool.h"
#include "ProjectToTargetTool.h"
#include "RemeshMeshTool.h"
#include "RemoveOccludedTrianglesTool.h"
#include "SimplifyMeshTool.h"
#include "SkeletalMeshEditingCache.h"
#include "SkeletalMeshEditorUtils.h"
#include "SmoothMeshTool.h"
#include "ToolTargetManager.h"
#include "WeldMeshEdgesTool.h"

#include "SkeletalMeshNotifier.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Components/SKMBackedDynaMeshComponent.h"
#include "SkeletalMesh/RefSkeletonPoser.h"

#include "SkeletalMesh/SkeletalMeshEditingInterface.h"
#include "SkeletalMesh/SkeletonEditingTool.h"
#include "SkeletalMesh/SkinWeightsBindingTool.h"
#include "SkeletalMesh/SkinWeightsPaintTool.h"
#include "ToolTargets/SkeletalMeshToolTarget.h"
#include "ToolTargets/SKMBackedDynaMeshComponentToolTarget.h"

#define LOCTEXT_NAMESPACE "SkeletalMeshModelingToolsEditorMode"

#if ENABLE_STYLUS_SUPPORT
// FStylusStateTracker registers itself as a listener for stylus events and implements
// the IToolStylusStateProviderAPI interface, which allows MeshSurfacePointTool implementations
 // to query for the pen pressure.
//
// This is kind of a hack. Unfortunately the current Stylus module is a Plugin so it
// cannot be used in the base ToolsFramework, and we need this in the Mode as a workaround.
//
class FStylusStateTracker : public IStylusMessageHandler, public IToolStylusStateProviderAPI
{
public:
	const IStylusInputDevice* ActiveDevice = nullptr;
	int32 ActiveDeviceIndex = -1;

	bool bPenDown = false;
	float ActivePressure = 1.0;

	FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->AddMessageHandler(*this);

		ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
		bPenDown = false;
	}

	virtual ~FStylusStateTracker()
	{
		if (GEditor)
		{
			if (UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>())
			{
				StylusSubsystem->RemoveMessageHandler(*this);
			}
		}
	}

	void OnStylusStateChanged(const FStylusState& NewState, int32 StylusIndex) override
	{
		if (ActiveDevice == nullptr)
		{
			UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
			ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
			bPenDown = false;
		}
		if (ActiveDevice != nullptr && ActiveDeviceIndex == StylusIndex)
		{
			bPenDown = NewState.IsStylusDown();
			ActivePressure = NewState.GetPressure();
		}
	}


	bool HaveActiveStylusState() const
	{
		return ActiveDevice != nullptr && bPenDown;
	}

	static const IStylusInputDevice* FindFirstPenDevice(const UStylusInputSubsystem* StylusSubsystem, int32& ActiveDeviceOut)
	{
		int32 NumDevices = StylusSubsystem->NumInputDevices();
		for (int32 k = 0; k < NumDevices; ++k)
		{
			const IStylusInputDevice* Device = StylusSubsystem->GetInputDevice(k);
			const TArray<EStylusInputType>& Inputs = Device->GetSupportedInputs();
			for (EStylusInputType Input : Inputs)
			{
				if (Input == EStylusInputType::Pressure)
				{
					ActiveDeviceOut = k;
					return Device;
				}
			}
		}
		return nullptr;
	}



	// IToolStylusStateProviderAPI implementation
	virtual float GetCurrentPressure() const override
	{
		return (ActiveDevice != nullptr && bPenDown) ? ActivePressure : 1.0f;
	}

};
#endif // ENABLE_STYLUS_SUPPORT

static void ShowEditorMessage(ELogVerbosity::Type InMessageType, const FText& InMessage)
{
	FNotificationInfo Notification(InMessage);
	Notification.bUseSuccessFailIcons = true;
	Notification.ExpireDuration = 5.0f;

	SNotificationItem::ECompletionState State = SNotificationItem::CS_Success;

	switch(InMessageType)
	{
	case ELogVerbosity::Warning:
		UE_LOG(LogSkeletalMeshModelingTools, Warning, TEXT("%s"), *InMessage.ToString());
		break;
	case ELogVerbosity::Error:
		State = SNotificationItem::CS_Fail;
		UE_LOG(LogSkeletalMeshModelingTools, Error, TEXT("%s"), *InMessage.ToString());
		break;
	default:
		break; // don't log anything unless a warning or error
	}
	
	FSlateNotificationManager::Get().AddNotification(Notification)->SetCompletionState(State);
}

// NOTE: This is a simple proxy at the moment. In the future we want to pull in more of the 
// modeling tools as we add support in the skelmesh storage.

const FEditorModeID USkeletalMeshModelingToolsEditorMode::Id("SkeletalMeshModelingToolsEditorMode");

FSkeletalMeshModelingToolsEditorModeNotifier::FSkeletalMeshModelingToolsEditorModeNotifier(USkeletalMeshModelingToolsEditorMode* InEditorMode)
	:EditorMode(InEditorMode)
{
}

void FSkeletalMeshModelingToolsEditorModeNotifier::HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType)
{
	if (Notifying())
	{
		return;
	}

	Notify(BoneNames, InNotifyType);
}

FSkeletalMeshModelingToolsEditorModeBinding::FSkeletalMeshModelingToolsEditorModeBinding(USkeletalMeshModelingToolsEditorMode* InEditorMode)
	: EditorMode(InEditorMode)
{
}

TSharedPtr<ISkeletalMeshNotifier> FSkeletalMeshModelingToolsEditorModeBinding::GetNotifier()
{
	if (!Notifier)
	{
		Notifier = MakeShared<FSkeletalMeshModelingToolsEditorModeNotifier>(EditorMode.Get());
	}
	
	return Notifier;
}

ISkeletalMeshEditorBinding::NameFunction FSkeletalMeshModelingToolsEditorModeBinding::GetNameFunction()
{
	// unused;
	return {};
}

TArray<FName> FSkeletalMeshModelingToolsEditorModeBinding::GetSelectedBones() const
{
	return EditorMode->GetSelectedBones();
}

USkeletalMeshModelingToolsEditorMode::USkeletalMeshModelingToolsEditorMode() 
{
	Info = FEditorModeInfo(Id, LOCTEXT("SkeletalMeshEditingMode", "Skeletal Mesh Editing"), FSlateIcon(), false);
}


USkeletalMeshModelingToolsEditorMode::USkeletalMeshModelingToolsEditorMode(FVTableHelper& Helper) :
	UBaseLegacyWidgetEdMode(Helper)
{
	
}


USkeletalMeshModelingToolsEditorMode::~USkeletalMeshModelingToolsEditorMode()
{
	// Implemented in the CPP file so that the destructor for TUniquePtr<FStylusStateTracker> gets correctly compiled.
}


void USkeletalMeshModelingToolsEditorMode::Initialize()
{
	UBaseLegacyWidgetEdMode::Initialize();
}


void USkeletalMeshModelingToolsEditorMode::Enter()
{
	UEdMode::Enter();

	UEditorInteractiveToolsContext* EditorInteractiveToolsContext = GetInteractiveToolsContext(EToolsContextScope::Editor);
	bDeactivateOnPIEStartStateToRestore = EditorInteractiveToolsContext->GetDeactivateToolsOnPIEStart();
	EditorInteractiveToolsContext->SetDeactivateToolsOnPIEStart(false);

	UEditorInteractiveToolsContext* InteractiveToolsContext = GetInteractiveToolsContext();

	if (TObjectPtr<UToolTargetManager> ToolTargetManager = InteractiveToolsContext->TargetManager)
	{
		// Special tool target factory to support fast tool switch for supported tools
		USkeletalMeshBackedDynamicMeshComponentToolTargetFactory* SKMDynaMeshFactory = NewObject<USkeletalMeshBackedDynamicMeshComponentToolTargetFactory>(ToolTargetManager);
		SKMDynaMeshFactory->Init(this);
		
		ToolTargetManager->AddTargetFactory(SKMDynaMeshFactory);
		ToolTargetManager->AddTargetFactory( NewObject<USkeletalMeshReadOnlyToolTargetFactory>(ToolTargetManager) );	
	
	}

#if ENABLE_STYLUS_SUPPORT
	StylusStateTracker = MakeUnique<FStylusStateTracker>();
#endif
	// register gizmo helper
	UE::TransformGizmoUtil::RegisterTransformGizmoContextObject(InteractiveToolsContext);
	UE::SkeletalMeshGizmoUtils::RegisterTransformGizmoContextObject(InteractiveToolsContext);
	UE::SkeletalMeshEditorUtils::RegisterEditorContextObject(InteractiveToolsContext);

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();

	RegisterTool(ToolManagerCommands.BeginPolyEditTool, TEXT("BeginPolyEditTool"), NewObject<UEditMeshPolygonsToolBuilder>());
	UEditMeshPolygonsToolBuilder* TriEditBuilder = NewObject<UEditMeshPolygonsToolBuilder>();
	TriEditBuilder->bTriangleMode = true;
	RegisterTool(ToolManagerCommands.BeginTriEditTool, TEXT("BeginTriEditTool"), TriEditBuilder);
	RegisterTool(ToolManagerCommands.BeginPolyDeformTool, TEXT("BeginPolyDeformTool"), NewObject<UDeformMeshPolygonsToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginHoleFillTool, TEXT("BeginHoleFillTool"), NewObject<UHoleFillToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginPolygonCutTool, TEXT("BeginPolyCutTool"), NewObject<UPolygonOnMeshToolBuilder>());

	RegisterTool(ToolManagerCommands.BeginSimplifyMeshTool, TEXT("BeginSimplifyMeshTool"), NewObject<USimplifyMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginRemeshMeshTool, TEXT("BeginRemeshMeshTool"), NewObject<URemeshMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginWeldEdgesTool, TEXT("BeginWeldEdgesTool"), NewObject<UWeldMeshEdgesToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginRemoveOccludedTrianglesTool, TEXT("BeginRemoveOccludedTrianglesTool"), NewObject<URemoveOccludedTrianglesToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginProjectToTargetTool, TEXT("BeginProjectToTargetTool"), NewObject<UProjectToTargetToolBuilder>());
	
	RegisterTool(ToolManagerCommands.BeginPolyGroupsTool, TEXT("BeginPolyGroupsTool"), NewObject<UConvertToPolygonsToolBuilder>());
	UMeshGroupPaintToolBuilder* MeshGroupPaintToolBuilder = NewObject<UMeshGroupPaintToolBuilder>();
#if ENABLE_STYLUS_SUPPORT 
	MeshGroupPaintToolBuilder->StylusAPI = StylusStateTracker.Get();
#endif
	RegisterTool(ToolManagerCommands.BeginMeshGroupPaintTool, TEXT("BeginMeshGroupPaintTool"), MeshGroupPaintToolBuilder);
	
	UMeshVertexSculptToolBuilder* MoveVerticesToolBuilder = NewObject<UMeshVertexSculptToolBuilder>();
#if ENABLE_STYLUS_SUPPORT
	MoveVerticesToolBuilder->StylusAPI = StylusStateTracker.Get();
#endif
	RegisterTool(ToolManagerCommands.BeginSculptMeshTool, TEXT("BeginSculptMeshTool"), MoveVerticesToolBuilder);

	UDynamicMeshSculptToolBuilder* DynaSculptToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	DynaSculptToolBuilder->bEnableRemeshing = true;
#if ENABLE_STYLUS_SUPPORT
	DynaSculptToolBuilder->StylusAPI = StylusStateTracker.Get();
#endif
	RegisterTool(ToolManagerCommands.BeginRemeshSculptMeshTool, TEXT("BeginRemeshSculptMeshTool"), DynaSculptToolBuilder);
	
	RegisterTool(ToolManagerCommands.BeginSmoothMeshTool, TEXT("BeginSmoothMeshTool"), NewObject<USmoothMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginOffsetMeshTool, TEXT("BeginOffsetMeshTool"), NewObject<UOffsetMeshToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginMeshSpaceDeformerTool, TEXT("BeginMeshSpaceDeformerTool"), NewObject<UMeshSpaceDeformerToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginLatticeDeformerTool, TEXT("BeginLatticeDeformerTool"), NewObject<ULatticeDeformerToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginDisplaceMeshTool, TEXT("BeginDisplaceMeshTool"), NewObject<UDisplaceMeshToolBuilder>());

	RegisterTool(ToolManagerCommands.BeginAttributeEditorTool, TEXT("BeginAttributeEditorTool"), NewObject<UAttributeEditorToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginMeshAttributePaintTool, TEXT("BeginMeshAttributePaintTool"), NewObject<UMeshAttributePaintToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginMeshVertexPaintTool, TEXT("BeginMeshVertexPaintTool"), NewObject<UMeshVertexPaintToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSkinWeightsPaintTool, TEXT("BeginSkinWeightsPaintTool"), NewObject<USkinWeightsPaintToolBuilder>());
	RegisterTool(ToolManagerCommands.BeginSkinWeightsBindingTool, TEXT("BeginSkinWeightsBindingTool"), NewObject<USkinWeightsBindingToolBuilder>());

	// Skeleton Editing
	RegisterTool(ToolManagerCommands.BeginSkeletonEditingTool, TEXT("BeginSkeletonEditingTool"), NewObject<USkeletonEditingToolBuilder>());

	// register extensions
	RegisterExtensions();
	
	// highlights skin weights tool by default
	GetInteractiveToolsContext()->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("BeginSkinWeightsPaintTool"));

	// record switching behavior to restore on exit
	ToolSwitchModeToRestoreOnExit = GetInteractiveToolsContext()->ToolManager->GetToolSwitchMode();
	// default to NOT applying changes to skeletal meshes when switching between tools without accepting
	GetInteractiveToolsContext()->ToolManager->SetToolSwitchMode(EToolManagerToolSwitchMode::CancelIfAble);

	GetInteractiveToolsContext()->ToolManager->OnToolPostBuild.AddUObject(this, &USkeletalMeshModelingToolsEditorMode::OnToolPostBuild);
	GetInteractiveToolsContext()->ToolManager->OnToolEndedWithStatus.AddUObject(this, &USkeletalMeshModelingToolsEditorMode::OnToolEndedWithStatus);

	CacheChangeCountWatcher.Initialize(
		[this]()
			{
				if (GetCurrentEditingCache())
				{
					return GetCurrentEditingCache()->GetEditingMeshComponent()->GetChangeCount();
				}
				return 0;
			},
		[&](int32 ChangeCount)
			{
				TypedToolkit.Pin()->RefreshMorphTargetManager();
			},
		0);
	
	CacheDynamicMeshSkeletonStatusWatcher.Initialize(
		[this]()
			{
				if (GetCurrentEditingCache())
				{
					return GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled();
				}
				return false;
			},
		[this](bool bEnableDynamicMeshSkeleton)
			{
				if (TypedToolkit.IsValid())
				{
					if (bEnableDynamicMeshSkeleton)
					{
						TypedToolkit.Pin()->ShowDynamicMeshSkeletonTree();
					}
					else
					{
						TypedToolkit.Pin()->ShowSkeletalMeshSkeletonTree();
					}
				}
			},
		false);
	
	CacheSkeletonChangeCountWatcher.Initialize(
		[this]()
			{
				if (GetCurrentEditingCache())
				{
					return GetCurrentEditingCache()->GetEditingMeshComponent()->GetSkeletonChangeTracker().GetChangeCount();
				}
				return 0;
			},
		[this](int32 ChangeCount)
			{
				if (GetCurrentEditingCache())
				{
					SkeletonReader->ExternalUpdate(GetCurrentEditingCache()->GetEditingMeshComponent()->GetRefSkeleton());
					GetModeBinding()->GetNotifier()->Notify({}, ESkeletalMeshNotifyType::HierarchyChanged);
				}
			},
		0);
	
}

UDebugSkelMeshComponent* USkeletalMeshModelingToolsEditorMode::GetSkelMeshComponent() const
{
	FToolBuilderState State; GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(State);
	UActorComponent* SkeletalMeshComponent = ToolBuilderUtil::FindFirstComponent(State, [&](UActorComponent* Component)
	{
		return IsValid(Component) && Component->IsA<UDebugSkelMeshComponent>();
	});

	return Cast<UDebugSkelMeshComponent>(SkeletalMeshComponent);
}


TSharedPtr<FTabManager> USkeletalMeshModelingToolsEditorMode::GetAssociatedTabManager()
{
	if (Editor.IsValid())
	{
		return Editor.Pin()->GetAssociatedTabManager();
	}

	return nullptr;
}

const TArray<FTransform>& USkeletalMeshModelingToolsEditorMode::GetComponentSpaceBoneTransforms() const
{
	return GetCurrentEditingCache()->GetComponentSpaceBoneTransforms();
}

void USkeletalMeshModelingToolsEditorMode::ToggleBoneManipulation(bool bEnable)
{
	GetCurrentEditingCache()->ToggleBoneManipulation(bEnable);
}

USkeletonModifier* USkeletalMeshModelingToolsEditorMode::GetSkeletonReader()
{
	return SkeletonReader;
}

TArray<FName> USkeletalMeshModelingToolsEditorMode::GetSelectedBones() const 
{
	return GetCurrentEditingCache()->GetSelectedBones();
}


FName USkeletalMeshModelingToolsEditorMode::GetEditingMorphTarget()
{
	if (!GetCurrentEditingCache()->GetMorphTargets().Contains(EditingMorphTarget))
	{
		return NAME_None;
	}

	return EditingMorphTarget;
}

void USkeletalMeshModelingToolsEditorMode::HandleSetEditingMorphTarget(FName InMorphTarget)
{
	FName OldEditingMorphTarget = EditingMorphTarget;
	EditingMorphTarget = InMorphTarget;

	GetCurrentEditingCache()->OverrideMorphTargetWeight(EditingMorphTarget, 1.0f);
	GetCurrentEditingCache()->ClearMorphTargetOverride(OldEditingMorphTarget);
}



TArray<FName> USkeletalMeshModelingToolsEditorMode::GetMorphTargets()
{
	return GetCurrentEditingCache()->GetMorphTargets();
}

float USkeletalMeshModelingToolsEditorMode::GetMorphTargetWeight(FName MorphTarget)
{
	return GetCurrentEditingCache()->GetMorphTargetWeight(MorphTarget);
}

FName USkeletalMeshModelingToolsEditorMode::HandleAddMorphTarget(FName InName)
{
	return GetCurrentEditingCache()->AddMorphTarget(InName);
}

FName USkeletalMeshModelingToolsEditorMode::HandleRenameMorphTarget(FName OldName, FName NewName)
{
	return GetCurrentEditingCache()->RenameMorphTarget(OldName, NewName);
}

void USkeletalMeshModelingToolsEditorMode::HandleRemoveMorphTargets(const TArray<FName>& Names)
{
	GetCurrentEditingCache()->RemoveMorphTargets(Names);
}

TArray<FName> USkeletalMeshModelingToolsEditorMode::HandleDuplicateMorphTargets(const TArray<FName>& Names)
{
	return GetCurrentEditingCache()->DuplicateMorphTargets(Names);
}

void USkeletalMeshModelingToolsEditorMode::HandleSetMorphTargetWeight(FName MorphTarget, float Weight)
{
	return GetCurrentEditingCache()->HandleSetMorphTargetWeight(MorphTarget, Weight);
}

bool USkeletalMeshModelingToolsEditorMode::GetMorphTargetAutoFill(FName MorphTarget)
{
	return GetCurrentEditingCache()->GetMorphTargetAutoFill(MorphTarget);
}

void USkeletalMeshModelingToolsEditorMode::HandleSetMorphTargetAutoFill(FName MorphTarget, bool bAutoFill, float PreviousOverrideWeight)
{
	return GetCurrentEditingCache()->HandleSetMorphTargetAutoFill(MorphTarget, bAutoFill, PreviousOverrideWeight);
}


void USkeletalMeshModelingToolsEditorMode::Exit()
{
	GetCurrentEditingCache()->ApplyChanges();
	GetCurrentEditingCache()->Destroy();
	CurrentEditingCache = nullptr;
	
	UEditorInteractiveToolsContext* InteractiveToolsContext = GetInteractiveToolsContext();
	UE::TransformGizmoUtil::DeregisterTransformGizmoContextObject(InteractiveToolsContext);
	UE::SkeletalMeshGizmoUtils::UnregisterTransformGizmoContextObject(InteractiveToolsContext);
	UE::SkeletalMeshEditorUtils::UnregisterEditorContextObject(InteractiveToolsContext);
	
	UEditorInteractiveToolsContext* EditorInteractiveToolsContext = GetInteractiveToolsContext(EToolsContextScope::Editor);
	EditorInteractiveToolsContext->SetDeactivateToolsOnPIEStart(bDeactivateOnPIEStartStateToRestore);

	// restore previous tool switching behavior
	GetInteractiveToolsContext()->ToolManager->SetToolSwitchMode(ToolSwitchModeToRestoreOnExit);

	if (Editor.IsValid())
	{
		Editor.Pin()->OnPreSaveAsset().RemoveAll(this);
		Editor.Pin()->OnPreSaveAssetAs().RemoveAll(this);
	}
	
#if ENABLE_STYLUS_SUPPORT
	StylusStateTracker = nullptr;
#endif

	UEdMode::Exit();
}

void USkeletalMeshModelingToolsEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);

	if (GetCurrentEditingCache())
	{
		auto SetBoneDrawConfig = [Viewport](FSkelDebugDrawConfig& Config)
			{
				if (const FAnimationViewportClient* AnimViewportClient = static_cast<FAnimationViewportClient*>(Viewport->GetClient()))
				{
					Config.BoneDrawMode = AnimViewportClient->GetBoneDrawMode();
					Config.BoneDrawSize = AnimViewportClient->GetBoneDrawSize();
				}
			};
		GetCurrentEditingCache()->Render(PDI, SetBoneDrawConfig);
	}
}

void USkeletalMeshModelingToolsEditorMode::RegisterExtensions()
{
	TArray<ISkeletalMeshModelingModeToolExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<ISkeletalMeshModelingModeToolExtension>(
		ISkeletalMeshModelingModeToolExtension::GetModularFeatureName());
	if (Extensions.IsEmpty())
	{
		return;
	}

	UEditorInteractiveToolsContext* ToolsContext = GetInteractiveToolsContext();
	
	FExtensionToolQueryInfo ExtensionQueryInfo;
	ExtensionQueryInfo.ToolsContext = ToolsContext;
	ExtensionQueryInfo.AssetAPI = nullptr;
#if ENABLE_STYLUS_SUPPORT
	ExtensionQueryInfo.StylusAPI = StylusStateTracker.Get();
#endif

	for (ISkeletalMeshModelingModeToolExtension* Extension: Extensions)
	{
		TArray<FExtensionToolDescription> ToolSet;
		Extension->GetExtensionTools(ExtensionQueryInfo, ToolSet);
		for (const FExtensionToolDescription& ToolInfo : ToolSet)
		{
			RegisterTool(ToolInfo.ToolCommand, ToolInfo.ToolName.ToString(), ToolInfo.ToolBuilder);
			ExtensionToolToInfo.Add(ToolInfo.ToolName.ToString(), ToolInfo);
		}

		TArray<TSubclassOf<UToolTargetFactory>> ExtensionToolTargetFactoryClasses;
		if (Extension->GetExtensionToolTargets(ExtensionToolTargetFactoryClasses))
		{
			for (const TSubclassOf<UToolTargetFactory>& ExtensionTargetFactoryClass : ExtensionToolTargetFactoryClasses)
			{
				ToolsContext->TargetManager->AddTargetFactory(NewObject<UToolTargetFactory>(GetToolManager(), ExtensionTargetFactoryClass.Get()));
			}
		}
	}
}

bool USkeletalMeshModelingToolsEditorMode::TryGetExtensionToolCommandGetter(
	UInteractiveToolManager* InManager, const UInteractiveTool* InTool, 
	TFunction<const UE::IInteractiveToolCommandsInterface& ()>& GetterOut) const
{
	if (!ensure(InManager && InTool)
		|| InManager->GetActiveTool(EToolSide::Mouse) != InTool)
	{
		return false;
	}

	const FString ToolName = InManager->GetActiveToolName(EToolSide::Mouse);
	if (ToolName.IsEmpty())
	{
		return false;
	}

	const FExtensionToolDescription* ToolDescription = ExtensionToolToInfo.Find(ToolName);
	if (!ToolDescription || !ToolDescription->ToolCommandsGetter)
	{
		return false;
	}
	
	GetterOut = ToolDescription->ToolCommandsGetter;
	return true;
}

void USkeletalMeshModelingToolsEditorMode::CreateToolkit()
{
	TSharedPtr<FSkeletalMeshModelingToolsEditorModeToolkit> NewToolKit = MakeShareable(new FSkeletalMeshModelingToolsEditorModeToolkit);
	Toolkit = NewToolKit;
	TypedToolkit = NewToolKit;
}

bool USkeletalMeshModelingToolsEditorMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	if (OtherModeID == FPersonaEditModes::SkeletonSelection)
	{
		return true;
	}
	
	return Super::IsCompatibleWith(OtherModeID);
}

void USkeletalMeshModelingToolsEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	Super::Tick(ViewportClient, DeltaTime);

	if (GetCurrentEditingCache())
	{
		GetCurrentEditingCache()->Tick();
	}

	CacheChangeCountWatcher.CheckAndUpdate();
	CacheDynamicMeshSkeletonStatusWatcher.CheckAndUpdate();
	CacheSkeletonChangeCountWatcher.CheckAndUpdate();
}

bool USkeletalMeshModelingToolsEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	if (GetCurrentEditingCache()->HandleClick(HitProxy))
	{
		return true;
	}
	
	// Making sure we can still select bones from viewport even if FPersonaEditModes::SkeletonSelection is deactivated
	if (TSharedPtr<ISkeletalMeshEditorBinding> EditorBindingPtr = GetEditorBinding())
	{
		TArray<FName> Selected;
		if (HitProxy && EditorBindingPtr->GetNameFunction())
		{
			if (TOptional<FName> BoneName = EditorBindingPtr->GetNameFunction()(HitProxy))
			{
				Selected.Emplace(*BoneName);
			}
		}

		GetModeBinding()->GetNotifier()->HandleNotification(Selected, ESkeletalMeshNotifyType::BonesSelected);
	}
	
	return Super::HandleClick(InViewportClient, HitProxy, Click);
}

void USkeletalMeshModelingToolsEditorMode::PostUndo()
{
	Super::PostUndo();

}

bool USkeletalMeshModelingToolsEditorMode::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const
{
	// if Tool supports custom Focus box, use that first
	if (GetToolManager()->HasAnyActiveTool())
	{
		UInteractiveTool* Tool = GetToolManager()->GetActiveTool(EToolSide::Mouse);
		IInteractiveToolCameraFocusAPI* FocusAPI = Cast<IInteractiveToolCameraFocusAPI>(Tool);
		if (FocusAPI && FocusAPI->SupportsWorldSpaceFocusBox())
		{
			InOutBox = FocusAPI->GetWorldSpaceFocusBox();
			return true;
		}
	}

	TArray<FName> Selection = GetCurrentEditingCache()->GetSelectedBones();
	
	
	// focus using selected bones in skel mesh editor
	if (!Selection.IsEmpty())
	{
		const FReferenceSkeleton& RefSkeleton = GetCurrentEditingCache()->GetEditingMeshComponent()->GetRefSkeleton();
		TArray<FTransform> WorldTransforms = GetCurrentEditingCache()->GetComponentSpaceBoneTransforms();
		FTransform MeshTransform = GetCurrentEditingCache()->GetTransform();
		for (FTransform& Transform : WorldTransforms)
		{
			Transform *= MeshTransform;
		}
		
		TArray<FName> AllChildren;

		for (const FName& BoneName: Selection)
		{
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			if (BoneIndex > INDEX_NONE && WorldTransforms.IsValidIndex(BoneIndex))
			{
				// enlarge box
				InOutBox += WorldTransforms[BoneIndex].GetLocation();

				// get direct children
				TArray<int32> Children;
				RefSkeleton.GetDirectChildBones(BoneIndex, Children);
				Algo::Transform(Children, AllChildren, [&RefSkeleton](int ChildrenIndex)
				{
					return RefSkeleton.GetBoneName(ChildrenIndex);
				});
			}
		}

		// enlarge box using direct children
		for (const FName& BoneName: AllChildren)
		{
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			if (BoneIndex > INDEX_NONE && WorldTransforms.IsValidIndex(BoneIndex))
			{
				InOutBox += WorldTransforms[BoneIndex].GetLocation();	
			}
		}
		
		return true; 
	}
	
	return Super::ComputeBoundingBoxForViewportFocus(Actor, PrimitiveComponent, InOutBox);
}

void USkeletalMeshModelingToolsEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FSkeletalMeshModelingToolsActionCommands::UpdateToolCommandBinding(Tool, Toolkit->GetToolkitCommands(), false);
	if (TryGetExtensionToolCommandGetter(Manager, Tool, ExtensionToolCommandsGetter) && ensure(ExtensionToolCommandsGetter))
	{
		ExtensionToolCommandsGetter().BindCommandsForCurrentTool(Toolkit->GetToolkitCommands(), Tool);
	}
}

void USkeletalMeshModelingToolsEditorMode::OnToolPostBuild( UInteractiveToolManager* InToolManager, EToolSide InSide, UInteractiveTool* InBuiltTool,
	UInteractiveToolBuilder* InToolBuilder, const FToolBuilderState& ToolState)
{
	// Tools can decide for themselves if they want to enable bone manipulation
	ToggleBoneManipulation(false);
}

void USkeletalMeshModelingToolsEditorMode::OnToolEndedWithStatus(UInteractiveToolManager* Manager, UInteractiveTool* Tool,
	EToolShutdownType ShutdownType)
{
	FSkeletalMeshModelingToolsActionCommands::UpdateToolCommandBinding(Tool, Toolkit->GetToolkitCommands(), true);
	if (ExtensionToolCommandsGetter)
	{
		ExtensionToolCommandsGetter().UnbindActiveCommands(Toolkit->GetToolkitCommands());
		ExtensionToolCommandsGetter = nullptr;
	}
	
	ToggleBoneManipulation(true);
}

bool USkeletalMeshModelingToolsEditorMode::ShouldToolStartBeAllowed(const FString& ToolIdentifier) const
{
	// in the base mode, this returns false if the level editor is in PIE or simulated
	// we allow all skeletal mesh editing tools to be started while running in PIE / simulate
	return true;
}

bool USkeletalMeshModelingToolsEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (GetCurrentEditingCache()->IsDynamicMeshBoneManipulationEnabled())
	{
		if (GetCurrentEditingCache()->GetFirstSelectedBoneIndex() != INDEX_NONE)
		{
			const UE::Widget::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();

			// we also allow undo/redo of bone manipulations
			if (WidgetMode == UE::Widget::WM_Rotate)
			{
				GEditor->BeginTransaction(LOCTEXT("RotateBone", "Rotate Bone"));
			}
			else
			{
				GEditor->BeginTransaction(LOCTEXT("TranslateBone", "Translate Bone"));
			}

			GetCurrentEditingCache()->GetSkeletonPoser()->BeginPoseChange();
			return true;
		}
	}
	
	return false;
}

bool USkeletalMeshModelingToolsEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (GetCurrentEditingCache()->GetSkeletonPoser()->IsRecordingPoseChange())
	{
		GetCurrentEditingCache()->GetSkeletonPoser()->EndPoseChange();
		
		GEditor->EndTransaction();
		return true;
	}
	return false;
}

bool USkeletalMeshModelingToolsEditorMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag,
	FRotator& InRot, FVector& InScale)
{
	if (GetCurrentEditingCache()->GetSkeletonPoser()->IsRecordingPoseChange())
	{
		// Actually change bone transform
		const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
		const ECoordSystem CoordSystem = InViewportClient->GetWidgetCoordSystemSpace();
		if (CurrentAxis != EAxisList::Type::None)
		{
			URefSkeletonPoser* Poser = GetCurrentEditingCache()->GetSkeletonPoser();

			int32 BoneIndex = GetCurrentEditingCache()->GetFirstSelectedBoneIndex();
			const FTransform& ComponentTransform = GetCurrentEditingCache()->GetTransform();
			const FTransform& ComponentSpaceTransform = Poser->GetComponentSpaceTransform(BoneIndex);
			FTransform BoneGlobal = ComponentSpaceTransform * ComponentTransform;

			FTransform BaseTransform = BoneGlobal;
			if (TOptional<FTransform> Additive = Poser->GetBoneAdditiveTransform(BoneIndex))
			{
				BaseTransform = BoneGlobal.GetRelativeTransformReverse(*Additive);
			}

			Poser->ModifyBoneAdditiveTransform(BoneIndex,
				[&BaseTransform, &InDrag, &InRot, &InScale, &CoordSystem](FTransform& InTransform)
				{
					FVector Offset = BaseTransform.TransformVector(InDrag);

					FVector RotAxis;
					float RotAngle;
					InRot.Quaternion().ToAxisAndAngle(RotAxis, RotAngle);

					FVector4 BoneSpaceAxis = BaseTransform.TransformVectorNoScale(RotAxis);

					//Calculate the new delta rotation
					FQuat DeltaQuat(BoneSpaceAxis, RotAngle);
					DeltaQuat.Normalize();

					FQuat NewRotation = (InTransform * FTransform(DeltaQuat)).GetRotation();

					FVector4 BoneSpaceScaleOffset;

					if (CoordSystem == COORD_World)
					{
						BoneSpaceScaleOffset = BaseTransform.TransformVector(InScale);
					}
					else
					{
						BoneSpaceScaleOffset = InScale;
					}

					InTransform.SetTranslation(InTransform.GetTranslation() + Offset);
					InTransform.SetRotation(NewRotation);
					InTransform.SetScale3D(InTransform.GetScale3D() + BoneSpaceScaleOffset);
				});

			return true;
		}	
	}

	return false;
}

void USkeletalMeshModelingToolsEditorMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View,
	FCanvas* Canvas)
{
	if (GetCurrentEditingCache())
	{
		// Draw bone name
		GetCurrentEditingCache()->DrawHUD(ViewportClient, Viewport, View, Canvas);
	}
	
}

bool USkeletalMeshModelingToolsEditorMode::AllowWidgetMove()
{
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		return ShouldDrawWidget();
	}
	return Super::AllowWidgetMove();
}

bool USkeletalMeshModelingToolsEditorMode::ShouldDrawWidget() const
{
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		if (GetCurrentEditingCache()->IsDynamicMeshBoneManipulationEnabled() && GetCurrentEditingCache()->GetFirstSelectedBoneIndex() != INDEX_NONE )
		{
			return true;
		}

		return false;
	}

	return Super::ShouldDrawWidget();
}

bool USkeletalMeshModelingToolsEditorMode::UsesTransformWidget() const
{
	// This function determines if the GetWidgetLocation() of this editor mode should be used
	
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		return true;
	}
	
	return false;
}

FVector USkeletalMeshModelingToolsEditorMode::GetWidgetLocation() const
{
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		int32 BoneIndex = GetCurrentEditingCache()->GetFirstSelectedBoneIndex();
		URefSkeletonPoser* Poser = GetCurrentEditingCache()->GetSkeletonPoser();
		FTransform ComponentTransform = GetCurrentEditingCache()->GetTransform();
		if (BoneIndex != INDEX_NONE)
		{
			FTransform ComponentSpaceTransform = Poser->GetComponentSpaceTransform(BoneIndex);
		
			FTransform WorldSpaceTransform = ComponentSpaceTransform * ComponentTransform;

			return WorldSpaceTransform.GetLocation();
		}
	
		return ComponentTransform.GetLocation();
	}

	return Super::GetWidgetLocation();
}



bool USkeletalMeshModelingToolsEditorMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		const bool bIsParentMode = Owner ? Owner->GetCoordSystem() == COORD_Parent : false;
	
		const FReferenceSkeleton& RefSkeleton = GetCurrentEditingCache()->GetEditingMeshComponent()->GetRefSkeleton();
		int32 BoneIndex = GetCurrentEditingCache()->GetFirstSelectedBoneIndex();
		URefSkeletonPoser* Poser = GetCurrentEditingCache()->GetSkeletonPoser();
		FTransform ComponentTransform = GetCurrentEditingCache()->GetTransform();
		if (RefSkeleton.IsValidIndex(BoneIndex))
		{
			if (bIsParentMode)
			{
				const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
				if (ParentIndex != INDEX_NONE)
				{
					BoneIndex = ParentIndex;
				}
			}
		
			FTransform BoneGlobalTransform = Poser->GetComponentSpaceTransform(BoneIndex) * ComponentTransform;	

			InMatrix = BoneGlobalTransform.ToMatrixNoScale().RemoveTranslation();
			return true;
		}

		return false;
	}
	
	return Super::GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

bool USkeletalMeshModelingToolsEditorMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (GetCurrentEditingCache()->IsDynamicMeshSkeletonEnabled())
	{
		return GetCustomDrawingCoordinateSystem(InMatrix, InData);
	}

	return Super::GetCustomInputCoordinateSystem(InMatrix, InData);
}

USkeletalMeshBackedDynamicMeshComponent* USkeletalMeshModelingToolsEditorMode::GetComponent(UObject* SourceObject)
{
	return GetCurrentEditingCache()->GetEditingMeshComponent();
}

bool USkeletalMeshModelingToolsEditorMode::ShouldCommitToSkeletalMeshOnToolCommit()
{
	return ApplyMode == EApplyMode::ApplyOnToolExit;
}

TSharedPtr<ISkeletalMeshEditorBinding> USkeletalMeshModelingToolsEditorMode::GetModeBinding()
{
	if (!ModeBinding.IsValid())
	{
		ModeBinding = MakeShared<FSkeletalMeshModelingToolsEditorModeBinding>(this);
	}

	return ModeBinding;
}

void USkeletalMeshModelingToolsEditorMode::SetEditorBinding(const TWeakPtr<ISkeletalMeshEditor>& InSkeletalMeshEditor)
{
	if (!InSkeletalMeshEditor.IsValid())
	{
		return;
	}
	
	EditorBinding = InSkeletalMeshEditor.Pin()->GetBinding();
	Editor = InSkeletalMeshEditor;
	Editor.Pin()->OnPreSaveAsset().AddUObject(this, &USkeletalMeshModelingToolsEditorMode::ApplyChanges);
	Editor.Pin()->OnPreSaveAssetAs().AddUObject(this, &USkeletalMeshModelingToolsEditorMode::ApplyChanges);

	SkeletalMesh = InSkeletalMeshEditor.Pin()->GetPersonaToolkit()->GetMesh();
	SkeletalMesh->GetOnMeshChanged().AddUObject(this, &USkeletalMeshModelingToolsEditorMode::HandleSkeletalMeshChanged);
	SkeletonReader = NewObject<USkeletonModifier>();
	SkeletonReader->SetSkeletalMesh(SkeletalMesh.Get());
	
	if (USkeletalMeshEditorContextObject* ContextObject = UE::SkeletalMeshEditorUtils::GetEditorContextObject(GetInteractiveToolsContext()))
	{
		ContextObject->Init(this);
	}
	
	OnInitializedDelegate.Broadcast();
}

TSharedPtr<ISkeletalMeshEditorBinding> USkeletalMeshModelingToolsEditorMode::GetEditorBinding()
{
	if (!EditorBinding.IsValid())
	{
		return nullptr;
	}
	
	return EditorBinding.Pin();
}

TSharedPtr<ISkeletalMeshEditor> USkeletalMeshModelingToolsEditorMode::GetEditor()
{
	if (!Editor.IsValid())
	{
		return nullptr;
	}

	return Editor.Pin();
}

USkeletalMesh* USkeletalMeshModelingToolsEditorMode::GetSkeletalMesh() const
{
	return SkeletalMesh.Get();
}

void USkeletalMeshModelingToolsEditorMode::HandleSkeletalMeshChanged()
{
	if (GetCurrentEditingCache())
	{
		if (!GetCurrentEditingCache()->IsApplyingChanges())
		{
			int32 ChangeCount = GetCurrentEditingCache()->GetEditingMeshComponent()->GetChangeCount();
			if (ChangeCount > 0)
			{
				FText Message = FText::Format(LOCTEXT("ExternalAssetChangeDiscardPendingToolChanges", "{0} change(s) discarded due to untracked external asset change"), ChangeCount);
				GetToolManager()->DisplayMessage(Message, EToolMessageLevel::UserWarning);
				ShowEditorMessage(ELogVerbosity::Type::Warning, Message);
			}
			
			RecreateEditingCache(GetEditingLOD());
		}
	}
	

}

bool USkeletalMeshModelingToolsEditorMode::CanSetEditingLOD()
{
	return !GetToolManager()->HasAnyActiveTool();
}

void USkeletalMeshModelingToolsEditorMode::SetEditingLOD(EMeshLODIdentifier InEditingLOD)
{
	if (GetCurrentEditingCache())
	{
		GetCurrentEditingCache()->ApplyChanges();
	}
	RecreateEditingCache(InEditingLOD);
	SkeletonReader->ExternalUpdate(GetCurrentEditingCache()->GetEditingMeshComponent()->GetRefSkeleton());

	TObjectPtr<UToolTargetManager> TargetManager = GetInteractiveToolsContext(EToolsContextScope::EdMode)->TargetManager;
	if (USkeletalMeshComponentToolTargetFactory* SkeletalMeshTargetFactory = TargetManager->FindFirstFactoryByType<USkeletalMeshComponentToolTargetFactory>())
	{
		SkeletalMeshTargetFactory->SetActiveEditingLOD(InEditingLOD);
	}
}

EMeshLODIdentifier USkeletalMeshModelingToolsEditorMode::GetEditingLOD()
{
	return GetCurrentEditingCache()->GetLOD();
}

void USkeletalMeshModelingToolsEditorMode::RecreateEditingCache(EMeshLODIdentifier InLOD)
{
	if (CurrentEditingCache)
	{
		CurrentEditingCache->Destroy();
		EditingCacheNotifierBindScope.Reset();
	}
	
	if (!Editor.IsValid())
	{
		return;
	}
	
	TSharedRef<IPersonaPreviewScene> PreviewScene = Editor.Pin()->GetPersonaToolkit()->GetPreviewScene();
	UWorld* PreviewWorld = PreviewScene->GetWorld();
	
	CurrentEditingCache = NewObject<USkeletalMeshEditingCache>();

	TWeakObjectPtr<USkeletalMeshModelingToolsEditorMode> WeakThis(this);
	
	USkeletalMeshEditingCache::FDelegates Delegates;
	Delegates.ToggleSkeletalMeshBoneManipulationDelegate.BindUObject(this, &USkeletalMeshModelingToolsEditorMode::ToggleSkeletalMeshBoneManipulation);
	Delegates.IsSkeletalMeshBoneManipulationEnabledDelegate.BindUObject(this, &USkeletalMeshModelingToolsEditorMode::IsSkeletalMeshBoneManipulationEnabled);
	CurrentEditingCache->Spawn(PreviewWorld, GetSkelMeshComponent(), InLOD, Delegates);


	using namespace UE::SkeletalMeshEditorUtils;
	EditingCacheNotifierBindScope.Reset(new FSkeletalMeshNotifierBindScope(GetModeBinding()->GetNotifier(), CurrentEditingCache->GetNotifier()));
}

USkeletalMeshEditingCache* USkeletalMeshModelingToolsEditorMode::GetCurrentEditingCache() const
{
	return CurrentEditingCache;
}

bool USkeletalMeshModelingToolsEditorMode::HasUnappliedChanges() const
{
	return GetCurrentEditingCache()->GetEditingMeshComponent()->IsDirty();
}

void USkeletalMeshModelingToolsEditorMode::ApplyChanges()
{
	GetCurrentEditingCache()->ApplyChanges();
}

void USkeletalMeshModelingToolsEditorMode::DiscardChanges()
{
	GetCurrentEditingCache()->DiscardChanges();
}

USkeletalMeshModelingToolsEditorMode::EApplyMode USkeletalMeshModelingToolsEditorMode::GetApplyMode() const
{
	return ApplyMode;
}

void USkeletalMeshModelingToolsEditorMode::SetApplyMode(EApplyMode InApplyMode)
{
	ApplyMode = InApplyMode;
	if (ApplyMode == EApplyMode::ApplyManually)
	{
		GetInteractiveToolsContext()->ToolManager->SetToolSwitchMode(EToolManagerToolSwitchMode::AcceptIfAble);
	}
	else
	{
		GetInteractiveToolsContext()->ToolManager->SetToolSwitchMode(EToolManagerToolSwitchMode::CancelIfAble);
	}
}

void USkeletalMeshModelingToolsEditorMode::HideSkeletonForTool()
{
	GetCurrentEditingCache()->HideSkeleton();
}

void USkeletalMeshModelingToolsEditorMode::ShowSkeletonForTool()
{
	GetCurrentEditingCache()->ShowSkeleton();
}

ISkeletalMeshEditingInterface* USkeletalMeshModelingToolsEditorMode::GetSkeletonInterface(UInteractiveTool* InTool)
{
	if (!IsValid(InTool) || !InTool->Implements<USkeletalMeshEditingInterface>())
	{
		return nullptr;
	}
	return static_cast<ISkeletalMeshEditingInterface*>(InTool->GetInterfaceAddress(USkeletalMeshEditingInterface::StaticClass()));
}


void USkeletalMeshModelingToolsEditorMode::ToggleSkeletalMeshBoneManipulation(bool bEnable)
{
	if (Owner)
	{
		if(bEnable)
		{
			Owner->ActivateMode(FPersonaEditModes::SkeletonSelection);
		}
		else
		{
			Owner->DeactivateMode(FPersonaEditModes::SkeletonSelection);
		}
	}
}

bool USkeletalMeshModelingToolsEditorMode::IsSkeletalMeshBoneManipulationEnabled()
{
	if (Owner)
	{
		return Owner->IsModeActive(FPersonaEditModes::SkeletonSelection);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
