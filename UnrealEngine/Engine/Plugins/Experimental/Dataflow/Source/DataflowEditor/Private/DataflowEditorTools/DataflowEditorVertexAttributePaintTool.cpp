// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowEditorTools/DataflowEditorVertexAttributePaintTool.h"

#include "BaseBehaviors/TwoAxisPropertyEditBehavior.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "ContextObjectStore.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"
#include "Dataflow/DataflowConstructionScene.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowEditorCollectionComponent.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowRenderingViewMode.h"
#include "DataflowEditorTools/DataflowEditorWeightMapPaintBrushOps.h"
#include "DataflowEditorTools/DataflowEditorWeightMapPaintTool.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "DynamicMesh/NonManifoldMappingSupport.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/Engine.h"
#include "InteractiveToolManager.h"
#include "Intersection/IntrLine2Line2.h"
#include "Intersection/IntrSegment2Segment2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Logging/LogMacros.h"
#include "ModelingToolTargetUtil.h"
#include "Polygon2.h"
#include "PreviewMesh.h"
#include "StaticMeshAttributes.h"
#include "Selections/MeshConnectedComponents.h"
#include "Selections/MeshVertexSelection.h"
#include "Spatial/PointHashGrid3.h"
#include "ToolSetupUtil.h"
#include "Util/BufferUtil.h"
#include "Widgets/Notifications/SNotificationList.h"



#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowEditorVertexAttributePaintTool)

DEFINE_LOG_CATEGORY_STATIC(LogDataflowEditorVertexAttributePaintTool, Warning, All);

#define LOCTEXT_NAMESPACE "DataflowEditorVertexAttributePaintTool"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UE::DataflowEditorVertexAttributePaintTool::CVars
{
	bool DataflowEditorUseNewWeightMapTool = true;
	FAutoConsoleVariableRef CVarDataflowEditorUseNewWeightMapTool(
		TEXT("p.DataflowEditor.UseNewWeightMapTool"),
		DataflowEditorUseNewWeightMapTool,
		TEXT("when on, the new weight map tool with improved UX will be used"));
}

namespace UE::DataflowEditorVertexAttributePaintTool::Private
{
	// probably should be something defined for the whole tool framework...
#if WITH_EDITOR
	static EAsyncExecution WeightPaintToolAsyncExecTarget = EAsyncExecution::LargeThreadPool;
#else
	static EAsyncExecution WeightPaintToolAsyncExecTarget = EAsyncExecution::ThreadPool;
#endif

	static bool HasManagedArrayCollection(const FDataflowNode* InDataflowNode, const TSharedPtr<UE::Dataflow::FEngineContext> Context)
	{
		static const FName CollectionTypeName = "FManagedArrayCollection";
		if (InDataflowNode && Context)
		{
			for (const FDataflowOutput* const Output : InDataflowNode->GetOutputs())
			{
				if (Output->GetType() == CollectionTypeName)
				{
					return true;
				}
			}
		}
		return false;
	}

	static void ShowEditorMessage(ELogVerbosity::Type InMessageType, const FText& InMessage)
	{
		FNotificationInfo Notification(InMessage);
		Notification.bUseSuccessFailIcons = true;
		Notification.ExpireDuration = 5.0f;

		SNotificationItem::ECompletionState State = SNotificationItem::CS_Success;

		switch (InMessageType)
		{
		case ELogVerbosity::Warning:
			UE_LOG(LogDataflowEditorVertexAttributePaintTool, Warning, TEXT("%s"), *InMessage.ToString());
			break;
		case ELogVerbosity::Error:
			State = SNotificationItem::CS_Fail;
			UE_LOG(LogDataflowEditorVertexAttributePaintTool, Error, TEXT("%s"), *InMessage.ToString());
			break;
		default:
			break; // don't log anything unless a warning or error
		}

		FSlateNotificationManager::Get().AddNotification(Notification)->SetCompletionState(State);
	}

	/**
		* A wrapper change that applies a given change to the unwrap canonical mesh of an input, and uses that
		* to update the other views. Causes a broadcast of OnCanonicalModified.
		*/
	class  FMeshChange : public FToolCommandChange
	{
	public:
		FMeshChange(UDynamicMeshComponent* DynamicMeshComponentIn, TUniquePtr<UE::Geometry::FDynamicMeshChange> DynamicMeshChangeIn)
			: DynamicMeshComponent(DynamicMeshComponentIn)
			, DynamicMeshChange(MoveTemp(DynamicMeshChangeIn))
		{
			ensure(DynamicMeshComponentIn);
			ensure(DynamicMeshChange);
		};

		virtual void Apply(UObject* Object) override
		{
			DynamicMeshChange->Apply(DynamicMeshComponent->GetMesh(), false);
		}

		virtual void Revert(UObject* Object) override
		{
			DynamicMeshChange->Apply(DynamicMeshComponent->GetMesh(), true);
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			return !(DynamicMeshComponent.IsValid() && DynamicMeshChange);
		}

		virtual FString ToString() const override
		{
			return TEXT("DataflowEditorVertexAttributePaintToolMeshChange");
		}

	protected:
		TWeakObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;
		TUniquePtr<UE::Geometry::FDynamicMeshChange> DynamicMeshChange;
	};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ToolBuilder
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void UDataflowEditorVertexAttributePaintToolBuilder::GetSupportedConstructionViewModes(const UDataflowContextObject& ContextObject, TArray<const UE::Dataflow::IDataflowConstructionViewMode*>& Modes) const
{
	if (!UE::DataflowEditorVertexAttributePaintTool::CVars::DataflowEditorUseNewWeightMapTool && FallbackToolBuilder)
	{
		if (const IDataflowEditorToolBuilder* FallbackDataflowToolBuilder = Cast<IDataflowEditorToolBuilder>(FallbackToolBuilder))
		{
			FallbackDataflowToolBuilder->GetSupportedConstructionViewModes(ContextObject, Modes);
			return;
		}
	}

	if (const FDataflowVertexAttributeEditableNode* SelectedNode = ContextObject.GetSelectedNodeOfType<FDataflowVertexAttributeEditableNode>())
	{
		if (const TSharedPtr<UE::Dataflow::FEngineContext>& EvalContext = ContextObject.GetDataflowContext())
		{
			TArray<FName> ViewModeNames;
			SelectedNode->GetSupportedViewModes(*EvalContext, ViewModeNames);

			const UE::Dataflow::FRenderingViewModeFactory& Factory = UE::Dataflow::FRenderingViewModeFactory::GetInstance();
			for (FName ViewModeName : ViewModeNames)
			{
				if (const UE::Dataflow::IDataflowConstructionViewMode* ViewMode = Factory.GetViewMode(ViewModeName))
				{
					Modes.Add(ViewMode);
				}
			}
		}
	}
}

bool UDataflowEditorVertexAttributePaintToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	if (!UE::DataflowEditorVertexAttributePaintTool::CVars::DataflowEditorUseNewWeightMapTool && FallbackToolBuilder)
	{
		return FallbackToolBuilder->CanBuildTool(SceneState);
	}
	
	if (UMeshSurfacePointMeshEditingToolBuilder::CanBuildTool(SceneState))
	{
		if (SceneState.SelectedComponents.Num() == 1)
		{
			if (TObjectPtr<UDataflowEditorCollectionComponent> Component = Cast<UDataflowEditorCollectionComponent>(SceneState.SelectedComponents[0]))
			{
				if (UDataflowBaseContent* ContextObject = SceneState.ToolManager->GetContextObjectStore()->FindContext<UDataflowBaseContent>())
				{
					if (ContextObject->GetSelectedNode() == Component->Node)
					{
						if (const TSharedPtr<UE::Dataflow::FEngineContext> EvaluationContext = ContextObject->GetDataflowContext())
						{
							if (const FDataflowNode* PrimarySelection = ContextObject->GetSelectedNodeOfType<FDataflowVertexAttributeEditableNode>())
							{
								return UE::DataflowEditorVertexAttributePaintTool::Private::HasManagedArrayCollection(PrimarySelection, EvaluationContext);
							}
						}
					}
				}
			}
		}
	}
	return false;
}

UMeshSurfacePointTool* UDataflowEditorVertexAttributePaintToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UDataflowContextObject* ContextObject = SceneState.ToolManager->GetContextObjectStore()->FindContext<UDataflowContextObject>();

	if (UE::DataflowEditorVertexAttributePaintTool::CVars::DataflowEditorUseNewWeightMapTool)
	{
		UDataflowEditorVertexAttributePaintTool* PaintTool = NewObject<UDataflowEditorVertexAttributePaintTool>(SceneState.ToolManager);
		PaintTool->SetEditorMode(Mode);
		PaintTool->SetWorld(SceneState.World);
		if (ContextObject)
		{
			PaintTool->SetDataflowEditorContextObject(ContextObject);
		}
		return PaintTool;
	}
	else if (FallbackToolBuilder)
	{
		return FallbackToolBuilder->CreateNewTool(SceneState);
	}

	// fallback : use the old tool
	// todo(ccaillaud) : remove this fall back when the new tool is on par with the old one 
	UDataflowEditorWeightMapPaintTool* PaintTool = NewObject<UDataflowEditorWeightMapPaintTool>(SceneState.ToolManager);
	PaintTool->SetEditorMode(Mode);
	PaintTool->SetWorld(SceneState.World);
	if (ContextObject)
	{
		PaintTool->SetDataflowEditorContextObject(ContextObject);
	}
	return PaintTool;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Properties
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UDataflowEditorVertexAttributePaintToolProperties::UDataflowEditorVertexAttributePaintToolProperties()
	: UInteractiveToolPropertySet()
{
	LoadConfig();

	if (DisplayProperties.ColorRamp.ColorRamp.IsEmpty())
	{
		CreateDefaultColorRamp();
	}

	constexpr bool bOnlyRGB = true;
	DisplayProperties.GreyScaleColorRamp.SetColorAtTime(0.0f, FLinearColor::Black, bOnlyRGB);
	DisplayProperties.GreyScaleColorRamp.SetColorAtTime(1.0f, FLinearColor::White, bOnlyRGB);

	DisplayProperties.WhiteColorRamp.SetColorAtTime(0.0f, FLinearColor::White, bOnlyRGB);
	DisplayProperties.WhiteColorRamp.SetColorAtTime(1.0f, FLinearColor::White, bOnlyRGB);
}

void UDataflowEditorVertexAttributePaintToolProperties::CreateDefaultColorRamp()
{
	constexpr FLinearColor HeatMapColors[] =
	{
		FLinearColor(0.8f, 0.4f, 0.8f), // Purple
		FLinearColor(0.0f, 0.0f, 0.5f), // Dark Blue
		FLinearColor(0.2f, 0.2f, 1.0f), // Light Blue
		FLinearColor(0.0f, 1.0f, 0.0f), // Green
		FLinearColor(1.0f, 1.0f, 0.0f), // Yellow
		FLinearColor(1.0f, 0.65f, 0.0f), // Orange
		FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), // Red
	};
	constexpr int32 NumColors = sizeof(HeatMapColors) / sizeof(FLinearColor);
	ensure(NumColors > 1);

	// Set ColorRamp
	FDataflowColorCurveOwner& Ramp = DisplayProperties.ColorRamp.ColorRamp;
	for (int32 Index = 0; Index < NumColors; ++Index)
	{
		const float Time = (float)Index / ((float)NumColors - 1.f);
		Ramp.SetColorAtTime(Time, HeatMapColors[Index], /*bOnlyRGB*/true);
	}
	Ramp.GetCurves()[3].CurveToEdit->AddKey(0.f, 1.f); // single alpha  value
}

const FDataflowEditorVertexAttributePaintToolBrushConfig& UDataflowEditorVertexAttributePaintToolProperties::GetBrushConfig(EDataflowEditorToolEditOperation BrushMode) const
{
	static FDataflowEditorVertexAttributePaintToolBrushConfig DefaultConfig;

	switch (BrushMode)
	{
	case EDataflowEditorToolEditOperation::Add:
		return BrushConfigAdd;

	case EDataflowEditorToolEditOperation::Replace:
		return BrushConfigReplace;

	case EDataflowEditorToolEditOperation::Multiply:
		return BrushConfigMultiply;

	case EDataflowEditorToolEditOperation::Relax:
		return BrushConfigRelax;

	case EDataflowEditorToolEditOperation::Invert:
	default:
		return DefaultConfig;
	};
}

void UDataflowEditorVertexAttributePaintToolProperties::SetBrushConfig(EDataflowEditorToolEditOperation BrushMode, const FDataflowEditorVertexAttributePaintToolBrushConfig& BrushConfig)
{
	switch (BrushMode)
	{
	case EDataflowEditorToolEditOperation::Add:
		BrushConfigAdd = BrushConfig;
		break;

	case EDataflowEditorToolEditOperation::Replace:
		BrushConfigReplace = BrushConfig;
		break;

	case EDataflowEditorToolEditOperation::Multiply:
		BrushConfigMultiply = BrushConfig;
		break;

	case EDataflowEditorToolEditOperation::Relax:
		BrushConfigRelax = BrushConfig;
		break;

	case EDataflowEditorToolEditOperation::Invert:
	default:
		// nothing to update
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Data
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FDataflowEditorVertexAttributePaintToolData::Setup(FDynamicMesh3& InMesh, FDataflowVertexAttributeEditableNode* InNodeToUpdate, TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject)
{
	// this should only be called once or have shutdown called in between 
	ensure(ActiveWeightMap == nullptr);
	NodeToUpdate = InNodeToUpdate;

	if (InDataflowEditorContextObject)
	{
		TArray<int32> ExtraMappingToWeight;
		TArray<TArray<int32>> ExtraMappingFromWeight;
		if (NodeToUpdate && InDataflowEditorContextObject)
		{
			if (const UE::Dataflow::IDataflowConstructionViewMode* ViewMode = InDataflowEditorContextObject->GetConstructionViewMode())
			{
				if (TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = InDataflowEditorContextObject->GetDataflowContext())
				{
					NodeToUpdate->GetExtraVertexMapping(*DataflowContext, ViewMode->GetName(), ExtraMappingToWeight, ExtraMappingFromWeight);
				}
			}
		}
		

		// Generate mapping array from and to non-manifiold and manifold mesh 
		if (const TSharedPtr<const FManagedArrayCollection> Collection = InDataflowEditorContextObject->GetRenderCollection())
		{
			using namespace UE::Dataflow;
			const UE::Geometry::FNonManifoldMappingSupport NonManifoldMapping(InMesh);
			const bool bHasNonManifoldMapping = NonManifoldMapping.IsNonManifoldVertexInSource();
			const bool bHasExtraMapping = (ExtraMappingToWeight.Num() != 0 && ExtraMappingFromWeight.Num() != 0);

			bHaveDynamicMeshToWeightConversion = bHasNonManifoldMapping || bHasExtraMapping;

			if (bHasNonManifoldMapping)
			{
				DynamicMeshToWeight.SetNumUninitialized(InMesh.VertexCount());
				WeightToDynamicMesh.Reset();
				WeightToDynamicMesh.SetNum(Collection->NumElements("Vertices")); // todo(ccaillaud) Do we realy need the render collection for that ?
				for (int32 DynamicMeshVert = 0; DynamicMeshVert < InMesh.VertexCount(); ++DynamicMeshVert)
				{
					DynamicMeshToWeight[DynamicMeshVert] = NonManifoldMapping.GetOriginalNonManifoldVertexID(DynamicMeshVert);
					if (bHasExtraMapping)
					{
						DynamicMeshToWeight[DynamicMeshVert] = ExtraMappingToWeight[DynamicMeshToWeight[DynamicMeshVert]];
					}
					if (WeightToDynamicMesh.IsValidIndex(DynamicMeshToWeight[DynamicMeshVert]))
					{
						WeightToDynamicMesh[DynamicMeshToWeight[DynamicMeshVert]].Add(DynamicMeshVert);
					}
					else
					{
						bHaveDynamicMeshToWeightConversion = false;
						UE_LOG(LogTemp, Warning, TEXT("Weight map misalignment, disabling remapping"));
						break;
					}
				}
			}
			else if (bHasExtraMapping)
			{
				DynamicMeshToWeight = ExtraMappingToWeight;
				WeightToDynamicMesh = ExtraMappingFromWeight;
			}
		}

		// Create an attribute layer to temporarily paint into
		const int NumAttributeLayers = InMesh.Attributes()->NumWeightLayers();
		InMesh.Attributes()->SetNumWeightLayers(NumAttributeLayers + 1);
		ActiveWeightMap = InMesh.Attributes()->GetWeightLayer(NumAttributeLayers);
		ActiveWeightMap->SetName(FName("PaintLayer"));

		if (NodeToUpdate)
		{
			// Setup DynamicMeshToWeight conversion and get Input weight map (if it exists)
			const int32 NumExpectedWeights = bHaveDynamicMeshToWeightConversion ? WeightToDynamicMesh.Num() : InMesh.MaxVertexID();

			TArray<float> CurrentWeights;
			// Get the setup attribute values
			if (TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = InDataflowEditorContextObject->GetDataflowContext())
			{
				NodeToUpdate->GetVertexAttributeValues(*DataflowContext, CurrentWeights);
			}
			ensure(CurrentWeights.Num() == NumExpectedWeights);

			if (bHaveDynamicMeshToWeightConversion)
			{
				if (WeightToDynamicMesh.Num() == CurrentWeights.Num())	// Only copy node weights if they match the number of mesh vertices
				{
					for (int32 WeightID = 0; WeightID < CurrentWeights.Num(); ++WeightID)
					{
						for (const int32 VertexID : WeightToDynamicMesh[WeightID])
						{
							ActiveWeightMap->SetValue(VertexID, &CurrentWeights[WeightID]);
						}
					}
				}
			}
			else
			{
				if (InMesh.MaxVertexID() == CurrentWeights.Num())	// Only copy node weights if they match the number of mesh vertices
				{
					for (int32 VertexID = 0; VertexID < CurrentWeights.Num(); ++VertexID)
					{
						ActiveWeightMap->SetValue(VertexID, &CurrentWeights[VertexID]);
					}
				}
			}
		}
	}
}

void FDataflowEditorVertexAttributePaintToolData::Shutdown()
{
	if (NodeToUpdate)
	{
		NodeToUpdate->Invalidate();
	}
}

void FDataflowEditorVertexAttributePaintToolData::BeginChange(UDynamicMeshComponent* Component)
{
	ensure(ActiveWeightEditChangeTracker == nullptr);
	if (Component)
	{
		ActiveWeightEditChangeTracker = MakeUnique<UE::Geometry::FDynamicMeshChangeTracker>(Component->GetMesh());
		ActiveWeightEditChangeTracker->BeginChange();
	}
}

TUniquePtr<UE::DataflowEditorVertexAttributePaintTool::Private::FMeshChange> FDataflowEditorVertexAttributePaintToolData::EndChange(UDynamicMeshComponent* Component)
{
	if (ensure(ActiveWeightEditChangeTracker) && Component)
	{
		using namespace UE::DataflowEditorVertexAttributePaintTool;
		TUniquePtr<UE::Geometry::FDynamicMeshChange> EditResult = ActiveWeightEditChangeTracker->EndChange();
		TUniquePtr<Private::FMeshChange> MeshChange = MakeUnique<Private::FMeshChange>(Component, MoveTemp(EditResult));
		ActiveWeightEditChangeTracker = nullptr;

		return MeshChange;
	}
	return {};
}

void FDataflowEditorVertexAttributePaintToolData::CancelChange()
{
	if (ensure(ActiveWeightEditChangeTracker))
	{
		ActiveWeightEditChangeTracker->EndChange();
		ActiveWeightEditChangeTracker = nullptr;
	}
}

float FDataflowEditorVertexAttributePaintToolData::GetValue(int32 VertexIdx) const
{
	float Value = 0.f;
	if (ActiveWeightMap && VertexIdx != IndexConstants::InvalidID)
	{
		ActiveWeightMap->GetValue(VertexIdx, &Value);
	}
	return Value;
}

float FDataflowEditorVertexAttributePaintToolData::GetAverageValue(const TArray<int32>& Vertices) const
{
	double AverageValue = 0.f;
	if (ActiveWeightMap)
	{
		int32 NumValidValues = 0;
		for (int32 VertexIdx : Vertices)
		{
			if (VertexIdx != IndexConstants::InvalidID)
			{
				float Value = 0.0f;
				ActiveWeightMap->GetValue(VertexIdx, &Value);
				AverageValue += (double)Value;
				++NumValidValues;
			}
		}
		if (NumValidValues > 0)
		{
			AverageValue = AverageValue / (double)NumValidValues;
		}
	}
	return (float)AverageValue;
}

void FDataflowEditorVertexAttributePaintToolData::UpdateNode(TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject, IToolsContextTransactionsAPI* TransactionAPI)
{
	if (ActiveWeightMap && NodeToUpdate)
	{
		if (const FDynamicMesh3* Mesh = ActiveWeightMap->GetParent())
		{
			if (TSharedPtr<UE::Dataflow::FEngineContext> DataflowContext = InDataflowEditorContextObject->GetDataflowContext())
			{
				// Save previous state for undo
				if (TObjectPtr<UDataflow> Dataflow = InDataflowEditorContextObject ? InDataflowEditorContextObject->GetDataflowAsset(): nullptr)
				{
					if (TransactionAPI)
					{
						TransactionAPI->AppendChange(
							Dataflow,
							NodeToUpdate->MakeEditNodeToolChange(),
							LOCTEXT("DataflowEditorVertexAttributePaintTool_ChangeDescription", "Update Weight Map Node")
						);
					}
					Dataflow->MarkPackageDirty();
				}

				// apply the new values to the node
				TArray<float> WeightsToApply;
				const int32 NumVertices = Mesh->VertexCount();
				WeightsToApply.SetNumUninitialized(NumVertices);
				for (int32 VertexID = 0; VertexID < NumVertices; ++VertexID)
				{
					ActiveWeightMap->GetValue(VertexID, &WeightsToApply[VertexID]);
				}
				// WeightsToApply represents the weights of the selected sub-mesh, DynamicMeshToWeight allow to remap properly to the full collection mesh
				NodeToUpdate->SetVertexAttributeValues(*DataflowContext, WeightsToApply, DynamicMeshToWeight);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Tool
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UBaseDynamicMeshComponent* UDataflowEditorVertexAttributePaintTool::GetSculptMeshComponent()
{
	return PreviewMesh ? Cast<UBaseDynamicMeshComponent>(PreviewMesh->GetRootComponent()) : nullptr;
}

void UDataflowEditorVertexAttributePaintTool::SetupBrushEditBehaviorSetup(ULocalTwoAxisPropertyEditInputBehavior& OutBehavior)
{
	// map the horizontal behavior to change the brush size
	UMeshSculptToolBase::MapHorizontalBrushEditBehaviorToBrushSize(OutBehavior);

	// map the vertical ehavior to change the attribiute value
	OutBehavior.VerticalProperty.GetValueFunc = [this]()
		{
			return ToolProperties->BrushProperties.AttributeValue;
		};

	OutBehavior.VerticalProperty.SetValueFunc = [this](float NewValue)
		{
			ToolProperties->BrushProperties.AttributeValue = FMath::Min(1.0f, FMath::Max(NewValue, 0.f));
#if WITH_EDITOR
			FPropertyChangedEvent PropertyChangedEvent(FDataflowEditorVertexAttributePaintToolBrushProperties::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, AttributeValue)));
			ToolProperties->PostEditChangeProperty(PropertyChangedEvent);
#endif
		};
	OutBehavior.VerticalProperty.Name = LOCTEXT("AttributeValue", "Value");
	OutBehavior.VerticalProperty.EditRate = 0.005f;
	OutBehavior.VerticalProperty.bEnabled = true;
}

void UDataflowEditorVertexAttributePaintTool::Setup()
{
	UMeshSculptToolBase::Setup();

	// todo(ccaillaud) : this should be a customized by the inherited class 
	// Get the selected node to read and write the vertex attribute
	FDataflowVertexAttributeEditableNode* NodeToUpdate = DataflowEditorContextObject->GetSelectedNodeOfType<FDataflowVertexAttributeEditableNode>();
	checkf(NodeToUpdate, TEXT("No Node is currently selected, or more than one node is selected"));

	// todo(ccaillaud) : this should be a parameter of the tool ? 
	SetToolDisplayName(LOCTEXT("ToolName", "Paint Weight Maps"));

	// Hide all meshes in the DataflowConstructionScene, as we will be painting onto our own Preview mesh
	if (const TSharedPtr<FDataflowConstructionScene> Scene = Mode->GetDataflowConstructionScene().Pin())
	{
		Scene->SetVisibility(false);
	}

	//OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshVerticesChanged.AddUObject(this, &UDataflowEditorVertexAttributePaintTool::OnDynamicMeshComponentChanged);

	// Create the preview mesh 
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(PreviewMesh, Target);

	// make sure we get a copy of the original dynamic mesh 
	FGetMeshParameters GetMeshParams;
	GetMeshParams.bWantMeshTangents = true;
	FDynamicMesh3 DynamicMeshCopy = UE::ToolTarget::GetDynamicMeshCopy(Target, GetMeshParams);
	PreviewMesh->UpdatePreview(MoveTemp(DynamicMeshCopy));

	// Assign materials
	FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Target);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	// make sure the dynamic mesh has all the necessary attributes
	FDynamicMesh3* Mesh = GetSculptMesh();
	Mesh->EnableVertexColors(FVector3f::One());
	Mesh->Attributes()->EnablePrimaryColors();
	Mesh->Attributes()->PrimaryColors()->CreatePerVertex(0.f);
	UE::Geometry::FAxisAlignedBox3d Bounds = Mesh->GetBounds(true);

	TFuture<void> PrecomputeFuture = Async(UE::DataflowEditorVertexAttributePaintTool::Private::WeightPaintToolAsyncExecTarget, [&]()
		{
			PrecomputeFilterData();
		});

	TFuture<void> OctreeFuture = Async(UE::DataflowEditorVertexAttributePaintTool::Private::WeightPaintToolAsyncExecTarget, [&]()
		{
			// initialize dynamic octree
			if (Mesh->TriangleCount() > 100000)
			{
				Octree.RootDimension = Bounds.MaxDim() / 10.0;
				Octree.SetMaxTreeDepth(4);
			}
			else
			{
				Octree.RootDimension = Bounds.MaxDim();
				Octree.SetMaxTreeDepth(8);
			}
			Octree.Initialize(Mesh);
		});

	// Setup mesh selector
	MeshSelector = NewObject<UToolMeshSelector>(this);
	auto OnSelectionChangedLambda = [this]() { /*OnSelectionModified();*/ };
	MeshSelector->InitialSetup(TargetWorld, this, OnSelectionChangedLambda);

	// compute the mesh description for the mesh selector to use
	{

		MeshSelector->SetMesh(PreviewMesh, FTransform::Identity);
	}

	// initialize brush radius range interval, brush properties
	UMeshSculptToolBase::InitializeBrushSizeRange(Bounds);

	// initialize properties
	ToolProperties = NewObject<UDataflowEditorVertexAttributePaintToolProperties>(this);
	{
		ToolProperties->WatchProperty(ToolProperties->BrushProperties.BrushSize,
			[this](float NewSize) { UMeshSculptToolBase::BrushProperties->BrushSize.AdaptiveSize = NewSize; });
		//ToolProperties->BrushProperties.BrushSize = UMeshSculptToolBase::BrushProperties->BrushSize.AdaptiveSize;
		ToolProperties->DisplayProperties.ColorRamp.ColorRamp.OnColorCurveChangedDelegate.AddUObject(this, &UDataflowEditorVertexAttributePaintTool::OnColorRampChanged);
		//ToolProperties->RestoreProperties(this);
	}
	AddToolPropertySource(ToolProperties);

	// make sure the selector is in sync with the settings
	MeshSelector->SetComponentSelectionMode(ToolProperties->SelectionProperties.ComponentSelectionMode);

	InitializeIndicator();

	// initialize our properties
	AddToolPropertySource(UMeshSculptToolBase::BrushProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::BrushProperties, false);
	UMeshSculptToolBase::BrushProperties->bShowPerBrushProps = false;
	UMeshSculptToolBase::BrushProperties->bShowFalloff = true;
	UMeshSculptToolBase::BrushProperties->bShowLazyness = false;
	UMeshSculptToolBase::BrushProperties->FlowRate = 0.0f;
	UMeshSculptToolBase::BrushProperties->GetOnModified().AddLambda(
		[this](UObject* InBrushProperties, FProperty* Property)
		{
			// make sure we update the settings back from updating the brush using B+Mouse drag
			if (InBrushProperties == UMeshSculptToolBase::BrushProperties)
			{
				if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushSize))
				{
					ToolProperties->BrushProperties.BrushSize = UMeshSculptToolBase::BrushProperties->BrushSize.AdaptiveSize;
				}
			}
		});
	CalculateBrushRadius();

	PaintBrushOpProperties = NewObject<UDataflowVertexAttributePaintBrushOpProps>(this);
	RegisterBrushType(PaintBrushId, LOCTEXT("Paint", "Paint"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FDataflowVertexAttributePaintBrushOp>(); }),
		PaintBrushOpProperties);

	// secondary brushes
	SmoothBrushOpProperties = NewObject<UDataflowWeightMapSmoothBrushOpProps>(this);
	RegisterSecondaryBrushType(SmoothBrushId, LOCTEXT("SmoothBrushType", "Smooth"),
		MakeUnique<FLambdaMeshSculptBrushOpFactory>([this]() { return MakeUnique<FDataflowWeightMapSmoothBrushOp>(); }),
		SmoothBrushOpProperties);

	AddToolPropertySource(UMeshSculptToolBase::ViewProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::ViewProperties, false);

	AddToolPropertySource(UMeshSculptToolBase::GizmoProperties);
	SetToolPropertySourceEnabled(UMeshSculptToolBase::GizmoProperties, false);

	// must call before updating brush type so that we register all brush properties?
	UMeshSculptToolBase::OnCompleteSetup();


	UpdateBrushType(ToolProperties->BrushProperties.BrushMode);

	// mesh element display is used to show seams and other features ofthe mesh
	MeshElementsDisplay = NewObject<UMeshElementsVisualizer>(this);
	MeshElementsDisplay->CreateInWorld(TargetWorld, FTransform::Identity);
	if (ensure(MeshElementsDisplay->Settings))
	{
		MeshElementsDisplay->Settings->bShowNormalSeams = false;
		MeshElementsDisplay->Settings->ThicknessScale = 0.5f;
		MeshElementsDisplay->Settings->BoundaryEdgeColor = FColor(164, 73, 164);
		MeshElementsDisplay->Settings->RestoreProperties(this, TEXT("DataflowEditorVertexAttributePaintTool"));
		AddToolPropertySource(MeshElementsDisplay->Settings);
	}
	MeshElementsDisplay->SetMeshAccessFunction([this](UMeshElementsVisualizer::ProcessDynamicMeshFunc ProcessFunc) {
		ProcessFunc(*GetSculptMesh());
		});

	// disable view properties
	SetViewPropertiesEnabled(false);
	UpdateMaterialMode(EMeshEditingMaterialModes::VertexColor);
	UpdateWireframeVisibility(false);
	UpdateFlatShadingSetting(false);

	VertexData.Setup(*Mesh, NodeToUpdate, DataflowEditorContextObject);
	
	PrecomputeFuture.Wait();
	OctreeFuture.Wait();

	// update colors
	UpdateVertexColorOverlay();
	//DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();

	ToolProperties->LoadConfig();
	SetBrushMode(ToolProperties->BrushProperties.BrushMode);
}

void UDataflowEditorVertexAttributePaintTool::SetDataflowEditorContextObject(TObjectPtr<UDataflowContextObject> InDataflowEditorContextObject)
{
	DataflowEditorContextObject = InDataflowEditorContextObject;
}

void UDataflowEditorVertexAttributePaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	//if (DynamicMeshComponent != nullptr)
	//{
	//	DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);
	//}

	if (MeshElementsDisplay)
	{
		if (ensure(MeshElementsDisplay->Settings))
		{
			MeshElementsDisplay->Settings->SaveProperties(this, TEXT("DataflowEditorWeightMapPaintTool2"));
		}
		MeshElementsDisplay->Disconnect();
	}

	if (ToolProperties)
	{
		ToolProperties->SaveProperties(this);
	}

	if (MeshSelector)
	{
		MeshSelector->Shutdown();
		MeshSelector = nullptr;
	}

	// we need to call this directly now because we use the preview mesh instead of the built-in component
	if (ShutdownType == EToolShutdownType::Accept)
	{
		CommitResult(GetSculptMeshComponent(), false);
	}

	VertexData.Shutdown();

	if (PreviewMesh)
	{
		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

	UMeshSculptToolBase::Shutdown(ShutdownType);
}


void UDataflowEditorVertexAttributePaintTool::CommitResult(UBaseDynamicMeshComponent* Component, bool bModifiedTopology)
{
	if (UInteractiveToolManager* ToolManager = GetToolManager())
	{
		ToolManager->BeginUndoTransaction(LOCTEXT("DataflowEditorVertexAttributePaintTool_TransactionName", "Paint Weights"));

		VertexData.UpdateNode(DataflowEditorContextObject, ToolManager->GetContextTransactionsAPI());

		ToolManager->EndUndoTransaction();
	}
}


void UDataflowEditorVertexAttributePaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	UMeshSculptToolBase::RegisterActions(ActionSet);

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 500,
		TEXT("PickWeightValueUnderCursor"),
		LOCTEXT("PickWeightValueUnderCursor", "Pick Weight Value"),
		LOCTEXT("PickWeightValueUnderCursorTooltip", "Set the active weight painting value to that currently under the cursor"),
		EModifierKey::Shift, EKeys::G,
		[this]() { bPendingPickWeight = true; });
};

void UDataflowEditorVertexAttributePaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	CalculateBrushRadius();
}


void UDataflowEditorVertexAttributePaintTool::IncreaseBrushRadiusAction()
{
	Super::IncreaseBrushRadiusAction();
	ToolProperties->BrushProperties.BrushSize = BrushProperties->BrushSize.AdaptiveSize;
	NotifyOfPropertyChangeByTool(ToolProperties);
}

void UDataflowEditorVertexAttributePaintTool::DecreaseBrushRadiusAction()
{
	Super::DecreaseBrushRadiusAction();
	ToolProperties->BrushProperties.BrushSize = BrushProperties->BrushSize.AdaptiveSize;
	NotifyOfPropertyChangeByTool(ToolProperties);
}

void UDataflowEditorVertexAttributePaintTool::IncreaseBrushRadiusSmallStepAction()
{
	Super::IncreaseBrushRadiusSmallStepAction();
	ToolProperties->BrushProperties.BrushSize = BrushProperties->BrushSize.AdaptiveSize;
	NotifyOfPropertyChangeByTool(ToolProperties);
}

void UDataflowEditorVertexAttributePaintTool::DecreaseBrushRadiusSmallStepAction()
{
	Super::DecreaseBrushRadiusSmallStepAction();
	ToolProperties->BrushProperties.BrushSize = BrushProperties->BrushSize.AdaptiveSize;
	NotifyOfPropertyChangeByTool(ToolProperties);
}

float UDataflowEditorVertexAttributePaintTool::GetBrushMinRadius() const
{
	return UMeshSculptToolBase::BrushProperties->BrushSize.WorldSizeRange.Min * 0.5f;
}

float UDataflowEditorVertexAttributePaintTool::GetBrushMaxRadius() const
{
	return UMeshSculptToolBase::BrushProperties->BrushSize.WorldSizeRange.Max * 0.5f;
}

void UDataflowEditorVertexAttributePaintTool::SetBrushMode(EDataflowEditorToolEditOperation NewBrushMode)
{
	if (ToolProperties)
	{
		const EDataflowEditorToolEditOperation CurrentBrushMode = ToolProperties->BrushProperties.BrushMode;
		ToolProperties->BrushProperties.BrushMode = NewBrushMode;

		// Save the current brush config 
		FDataflowEditorVertexAttributePaintToolBrushConfig BrushConfigToSave;
		BrushConfigToSave.BrushSize = ToolProperties->BrushProperties.BrushSize;
		BrushConfigToSave.Value = ToolProperties->BrushProperties.AttributeValue;
		ToolProperties->SetBrushConfig(CurrentBrushMode, BrushConfigToSave);

		// Load the new brush config 
		const FDataflowEditorVertexAttributePaintToolBrushConfig& NewBrushConfig = ToolProperties->GetBrushConfig(NewBrushMode);
		ToolProperties->BrushProperties.BrushSize = NewBrushConfig.BrushSize;
		ToolProperties->BrushProperties.AttributeValue = NewBrushConfig.Value;

		// Send notification for the changed properties
		FPropertyChangedEvent BrushModePropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushMode)));
		ToolProperties->PostEditChangeProperty(BrushModePropertyChangedEvent);
		FPropertyChangedEvent BrushSizePropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, BrushSize)));
		ToolProperties->PostEditChangeProperty(BrushSizePropertyChangedEvent);
		FPropertyChangedEvent ValuePropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolBrushProperties, AttributeValue)));
		ToolProperties->PostEditChangeProperty(ValuePropertyChangedEvent);

		ToolProperties->SaveConfig();
	}
}

bool UDataflowEditorVertexAttributePaintTool::IsInBrushMode() const
{
	return (ToolProperties->EditingMode == EDataflowEditorToolEditMode::Brush);
}

bool UDataflowEditorVertexAttributePaintTool::IsVolumetricBrush() const
{
	return (ToolProperties && ToolProperties->BrushProperties.BrushAreaMode == EMeshVertexPaintBrushAreaType::Volumetric);
}

void UDataflowEditorVertexAttributePaintTool::OnBeginStroke(const FRay& WorldRay)
{
	if (!VertexData.IsValid())
	{
		return;
	}

	UpdateBrushPosition(WorldRay);

	if (PaintBrushOpProperties)
	{
		PaintBrushOpProperties->AttributeValue = ToolProperties->BrushProperties.AttributeValue;
		if (GetInInvertStroke())
		{
			PaintBrushOpProperties->AttributeValue = FMath::Clamp(1.0f - PaintBrushOpProperties->AttributeValue, 0.f, 1.f);
		}
		PaintBrushOpProperties->EditOperation = ToolProperties->BrushProperties.BrushMode;
		PaintBrushOpProperties->Strength = 1.0f; // Attribute painting only need the attribute value 
	}

	// initialize first "Last Stamp", so that we can assume all stamps in stroke have a valid previous stamp
	LastStamp.WorldFrame = GetBrushFrameWorld();
	LastStamp.LocalFrame = GetBrushFrameLocal();
	LastStamp.Radius = GetCurrentBrushRadius();
	LastStamp.Falloff = GetCurrentBrushFalloff();
	LastStamp.Direction = GetInInvertStroke() ? -1.0 : 1.0;
	LastStamp.Depth = GetCurrentBrushDepth();
	LastStamp.Power = GetActivePressure() * GetCurrentBrushStrength();
	LastStamp.TimeStamp = FDateTime::Now();

	FSculptBrushOptions SculptOptions;
	SculptOptions.ConstantReferencePlane = GetCurrentStrokeReferencePlane();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();
	UseBrushOp->ConfigureOptions(SculptOptions);
	UseBrushOp->BeginStroke(GetSculptMesh(), LastStamp, VertexROI);

	AccumulatedTriangleROI.Reset();

	// begin change here? or wait for first stamp?
	BeginChange();
}

void UDataflowEditorVertexAttributePaintTool::OnEndStroke()
{
	if (!VertexData.IsValid())
	{
		return;
	}

	GetActiveBrushOp()->EndStroke(GetSculptMesh(), LastStamp, VertexROI);

	UpdateVertexColorOverlay(&TriangleROI);
	//DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);

	// close change record
	EndChange();
}

void UDataflowEditorVertexAttributePaintTool::OnCancelStroke()
{
	if (const TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp())
	{
		UseBrushOp->CancelStroke();
	}
	CancelChange();
}

void UDataflowEditorVertexAttributePaintTool::UpdateROI(const FSculptBrushStamp& BrushStamp)
{
	SCOPE_CYCLE_COUNTER(VertexAttributePaintTool_UpdateROI);

	const FVector3d& BrushPos = BrushStamp.LocalFrame.Origin;
	const FDynamicMesh3* Mesh = GetSculptMesh();
	const float RadiusSqr = GetCurrentBrushRadius() * GetCurrentBrushRadius();
	UE::Geometry::FAxisAlignedBox3d BrushBox(
		BrushPos - GetCurrentBrushRadius() * FVector3d::One(),
		BrushPos + GetCurrentBrushRadius() * FVector3d::One());

	TriangleROI.Reset();

	int32 CenterTID = GetBrushTriangleID();
	if (Mesh->IsTriangle(CenterTID))
	{
		TriangleROI.Add(CenterTID);
	}

	FVector3d CenterNormal = Mesh->IsTriangle(CenterTID) ? TriNormals[CenterTID] : FVector3d::One();		// One so that normal check always passes

	const bool bVolumetric = IsVolumetricBrush();
	const bool bUseAngleThreshold = ToolProperties->BrushProperties.AngleThreshold < 180.0f;
	const double DotAngleThreshold = FMathd::Cos(ToolProperties->BrushProperties.AngleThreshold * FMathd::DegToRad);
	const bool bStopAtUVSeams = ToolProperties->BrushProperties.bUVSeams;
	const bool bStopAtNormalSeams = ToolProperties->BrushProperties.bNormalSeams;

	auto CheckEdgeCriteria = [&](int32 t1, int32 t2) -> bool
		{
			if (bUseAngleThreshold == false || CenterNormal.Dot(TriNormals[t2]) > DotAngleThreshold)
			{
				int32 eid = Mesh->FindEdgeFromTriPair(t1, t2);
				if (bStopAtUVSeams == false || UVSeamEdges[eid] == false)
				{
					if (bStopAtNormalSeams == false || NormalSeamEdges[eid] == false)
					{
						return true;
					}
				}
			}
			return false;
		};

	if (bVolumetric)
	{
		Octree.RangeQuery(BrushBox,
			[&](int TriIdx) {
				if ((Mesh->GetTriCentroid(TriIdx) - BrushPos).SquaredLength() < RadiusSqr)
				{
					TriangleROI.Add(TriIdx);
				}
			});
	}
	else
	{
		if (Mesh->IsTriangle(CenterTID))
		{
			TArray<int32> StartROI;
			StartROI.Add(CenterTID);
			UE::Geometry::FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, TriangleROI, &TempROIBuffer,
				[&](int t1, int t2)
				{
					if ((Mesh->GetTriCentroid(t2) - BrushPos).SquaredLength() < RadiusSqr)
					{
						return CheckEdgeCriteria(t1, t2);
					}
					return false;
				});
		}
	}

	if (VertexData.HaveDynamicMeshToWeightConversion())
	{
		// Find triangles whose vertices map to the same welded vertex as any vertex in VertexSetBuffer and add them to TriangleROI
		for (const int VertexID : VertexSetBuffer)
		{
			for (const int OtherVertexID : VertexData.GetWeightToDynamicMesh()[VertexData.GetDynamicMeshToWeight()[VertexID]])
			{
				if (OtherVertexID != VertexID)
				{
					Mesh->EnumerateVertexTriangles(OtherVertexID, [this](int32 AdjacentTri)
						{
							TriangleROI.Add(AdjacentTri);
						});
				}
			}
		}

	}

	// Construct ROI vertex set
	VertexSetBuffer.Reset();
	for (int32 tid : TriangleROI)
	{
		UE::Geometry::FIndex3i Tri = Mesh->GetTriangle(tid);
		VertexSetBuffer.Add(Tri.A);  VertexSetBuffer.Add(Tri.B);  VertexSetBuffer.Add(Tri.C);
	}

	// Apply visibility filter
	if (ToolProperties->BrushProperties.VisibilityFilter != EDataflowEditorToolVisibilityType::None)
	{
		TArray<int32> ResultBuffer;
		ApplyVisibilityFilter(VertexSetBuffer, TempROIBuffer, ResultBuffer);
	}

	VertexROI.SetNum(0, EAllowShrinking::No);
	//TODO: If we paint a 2D projection of UVs, these will need to be the 2D vertices not the 3D original mesh vertices
	BufferUtil::AppendElements(VertexROI, VertexSetBuffer);

	// Construct ROI triangle and weight buffers
	ROITriangleBuffer.Reserve(TriangleROI.Num());
	ROITriangleBuffer.SetNum(0, EAllowShrinking::No);
	for (int32 tid : TriangleROI)
	{
		ROITriangleBuffer.Add(tid);
	}
	ROIWeightValueBuffer.SetNum(VertexROI.Num(), EAllowShrinking::No);
	SyncWeightBufferWithMesh(Mesh);
}

bool UDataflowEditorVertexAttributePaintTool::UpdateStampPosition(const FRay& WorldRay)
{
	CalculateBrushRadius();

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
	case ESculptBrushOpTargetType::TargetMesh:
		UpdateBrushPositionOnSculptMesh(WorldRay, true);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		check(false);
		UpdateBrushPositionOnActivePlane(WorldRay);
		break;
	}

	if (UseBrushOp->GetAlignStampToView() || IsVolumetricBrush())
	{
		AlignBrushToView();
	}

	CurrentStamp = LastStamp;
	CurrentStamp.DeltaTime = FMathd::Min((FDateTime::Now() - LastStamp.TimeStamp).GetTotalSeconds(), 1.0);
	CurrentStamp.WorldFrame = GetBrushFrameWorld();
	CurrentStamp.LocalFrame = GetBrushFrameLocal();
	CurrentStamp.Power = GetActivePressure() * GetCurrentBrushStrength();

	CurrentStamp.PrevLocalFrame = LastStamp.LocalFrame;
	CurrentStamp.PrevWorldFrame = LastStamp.WorldFrame;

	FVector3d MoveDelta = CurrentStamp.LocalFrame.Origin - CurrentStamp.PrevLocalFrame.Origin;

	if (UseBrushOp->IgnoreZeroMovements() && MoveDelta.SquaredLength() < 0.1 * CurrentBrushRadius)
	{
		return false;
	}

	return true;
}

bool UDataflowEditorVertexAttributePaintTool::ApplyStamp()
{
	SCOPE_CYCLE_COUNTER(VertexAttributePaintTool_ApplyStamp);

	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	// yuck
	FDataflowMeshVertexWeightMapEditBrushOp* WeightBrushOp = (FDataflowMeshVertexWeightMapEditBrushOp*)UseBrushOp.Get();
	WeightBrushOp->bApplyRadiusLimit = IsInBrushMode();

	FDynamicMesh3* Mesh = GetSculptMesh();
	WeightBrushOp->ApplyStampByVertices(Mesh, CurrentStamp, VertexROI, ROIWeightValueBuffer);

	bool bUpdated = SyncMeshWithWeightBuffer(Mesh);

	LastStamp = CurrentStamp;
	LastStamp.TimeStamp = FDateTime::Now();

	return bUpdated;
}

bool UDataflowEditorVertexAttributePaintTool::SyncMeshWithWeightBuffer(FDynamicMesh3* Mesh)
{
	int32 NumModified = 0;
	if (VertexData.IsValid())
	{
		// change update could be async here if we collected array of <idx,orig,new> and dispatched independenlty
		const int32 NumVertexROI = VertexROI.Num();
		for (int32 VertexROIIndex = 0; VertexROIIndex < NumVertexROI; ++VertexROIIndex)
		{
			const int VertIdx = VertexROI[VertexROIIndex];
			if (VertIdx != INDEX_NONE)
			{
				const float NewValue = ROIWeightValueBuffer[VertexROIIndex];
				VertexData.SetValue(VertIdx,
					[NewValue](UE::Geometry::FDynamicMeshWeightAttribute& AttributeMap, int32 AttributeIndex)
					{
						return NewValue;
					});
				NumModified++;
			}
		}
	}
	return (NumModified > 0);
}

bool UDataflowEditorVertexAttributePaintTool::SyncWeightBufferWithMesh(const FDynamicMesh3* Mesh)
{
	int32 NumModified = 0;
	if (VertexData.IsValid())
	{
		// change update could be async here if we collected array of <idx,orig,new> and dispatched independenlty
		const int32 NumVertexROI = VertexROI.Num();
		for (int32 VertexROIIndex = 0; VertexROIIndex < NumVertexROI; ++VertexROIIndex)
		{
			const int VertIdx = VertexROI[VertexROIIndex];
			const float CurWeight = VertexData.GetValue(VertIdx);
			if (ROIWeightValueBuffer[VertexROIIndex] != CurWeight)
			{
				ROIWeightValueBuffer[VertexROIIndex] = CurWeight;
				NumModified++;
			}
		}
	}
	return (NumModified > 0);
}

// TODO(ccaillaud) : move this to a common  place
namespace UE::DataflowEditorVertexAttributePaintTool::Private
{
	template<typename RealType>
	static bool FindPolylineSelfIntersection(
		const TArray<UE::Math::TVector2<RealType>>& Polyline,
		UE::Math::TVector2<RealType>& IntersectionPointOut,
		UE::Geometry::FIndex2i& IntersectionIndexOut,
		bool bParallel = true)
	{
		int32 N = Polyline.Num();
		std::atomic<bool> bSelfIntersects(false);
		ParallelFor(N - 1, [&](int32 i)
			{
				UE::Geometry::TSegment2<RealType> SegA(Polyline[i], Polyline[i + 1]);
				for (int32 j = i + 2; j < N - 1 && bSelfIntersects == false; ++j)
				{
					UE::Geometry::TSegment2<RealType> SegB(Polyline[j], Polyline[j + 1]);
					if (SegA.Intersects(SegB) && bSelfIntersects == false)
					{
						bool ExpectedValue = false;
						if (std::atomic_compare_exchange_strong(&bSelfIntersects, &ExpectedValue, true))
						{
							UE::Geometry::TIntrSegment2Segment2<RealType> Intersection(SegA, SegB);
							Intersection.Find();
							IntersectionPointOut = Intersection.Point0;
							IntersectionIndexOut = UE::Geometry::FIndex2i(i, j);
							return;
						}
					}
				}
			}, (bParallel) ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);

		return bSelfIntersects;
	}



	template<typename RealType>
	static bool FindPolylineSegmentIntersection(
		const TArray<UE::Math::TVector2<RealType>>& Polyline,
		const UE::Geometry::TSegment2<RealType>& Segment,
		UE::Math::TVector2<RealType>& IntersectionPointOut,
		int& IntersectionIndexOut)
	{

		int32 N = Polyline.Num();
		for (int32 i = 0; i < N - 1; ++i)
		{
			UE::Geometry::TSegment2<RealType> PolySeg(Polyline[i], Polyline[i + 1]);
			if (Segment.Intersects(PolySeg))
			{
				UE::Geometry::TIntrSegment2Segment2<RealType> Intersection(Segment, PolySeg);
				Intersection.Find();
				IntersectionPointOut = Intersection.Point0;
				IntersectionIndexOut = i;
				return true;
			}
		}
		return false;
	}


	bool ApproxSelfClipPolyline(TArray<FVector2f>& Polyline)
	{
		int32 N = Polyline.Num();

		// handle already-closed polylines
		if (UE::Geometry::Distance(Polyline[0], Polyline[N - 1]) < 0.0001f)
		{
			return true;
		}

		FVector2f IntersectPoint;
		UE::Geometry::FIndex2i IntersectionIndex(-1, -1);
		bool bSelfIntersects = FindPolylineSelfIntersection(Polyline, IntersectPoint, IntersectionIndex);
		if (bSelfIntersects)
		{
			TArray<FVector2f> NewPolyline;
			NewPolyline.Add(IntersectPoint);
			for (int32 i = IntersectionIndex.A; i <= IntersectionIndex.B; ++i)
			{
				NewPolyline.Add(Polyline[i]);
			}
			NewPolyline.Add(IntersectPoint);
			Polyline = MoveTemp(NewPolyline);
			return true;
		}


		FVector2f StartDirOut = UE::Geometry::Normalized(Polyline[0] - Polyline[1]);
		UE::Geometry::FLine2f StartLine(Polyline[0], StartDirOut);
		FVector2f EndDirOut = UE::Geometry::Normalized(Polyline[N - 1] - Polyline[N - 2]);
		UE::Geometry::FLine2f EndLine(Polyline[N - 1], EndDirOut);
		UE::Geometry::FIntrLine2Line2f LineIntr(StartLine, EndLine);
		bool bIntersects = false;
		if (LineIntr.Find())
		{
			bIntersects = LineIntr.IsSimpleIntersection() && (LineIntr.Segment1Parameter > 0) && (LineIntr.Segment2Parameter > 0);
			if (bIntersects)
			{
				Polyline.Add(StartLine.PointAt(LineIntr.Segment1Parameter));
				Polyline.Add(StartLine.Origin);
				return true;
			}
		}


		UE::Geometry::FAxisAlignedBox2f Bounds;
		for (const FVector2f& P : Polyline)
		{
			Bounds.Contain(P);
		}
		float Size = Bounds.DiagonalLength();

		FVector2f StartPos = Polyline[0] + 0.001f * StartDirOut;
		if (FindPolylineSegmentIntersection(Polyline, UE::Geometry::FSegment2f(StartPos, StartPos + 2 * Size * StartDirOut), IntersectPoint, IntersectionIndex.A))
		{
			return true;
		}

		FVector2f EndPos = Polyline[N - 1] + 0.001f * EndDirOut;
		if (FindPolylineSegmentIntersection(Polyline, UE::Geometry::FSegment2f(EndPos, EndPos + 2 * Size * EndDirOut), IntersectPoint, IntersectionIndex.A))
		{
			return true;
		}

		return false;
	}
}


void UDataflowEditorVertexAttributePaintTool::OnPolyLassoFinished(const FCameraPolyLasso& Lasso, bool bCanceled)
{
	// construct polyline
	TArray<FVector2f> Polyline;
	for (FVector2D Pos : Lasso.Polyline)
	{
		Polyline.Add((FVector2f)Pos);
	}
	int32 N = Polyline.Num();
	if (N < 2)
	{
		return;
	}

	// Try to clip polyline to be closed, or closed-enough for winding evaluation to work.
	// If that returns false, the polyline is "too open". In that case we will extend
	// outwards from the endpoints and then try to create a closed very large polygon
	if (UE::DataflowEditorVertexAttributePaintTool::Private::ApproxSelfClipPolyline(Polyline) == false)
	{
		FVector2f StartDirOut = UE::Geometry::Normalized(Polyline[0] - Polyline[1]);
		UE::Geometry::FLine2f StartLine(Polyline[0], StartDirOut);
		FVector2f EndDirOut = UE::Geometry::Normalized(Polyline[N - 1] - Polyline[N - 2]);
		UE::Geometry::FLine2f EndLine(Polyline[N - 1], EndDirOut);

		// if we did not intersect, we are in ambiguous territory. Check if a segment along either end-direction
		// intersects the polyline. If it does, we have something like a spiral and will be OK. 
		// If not, make a closed polygon by interpolating outwards from each endpoint, and then in perp-directions.
		UE::Geometry::FPolygon2f Polygon(Polyline);
		float PerpSign = Polygon.IsClockwise() ? -1.0 : 1.0;

		Polyline.Insert(StartLine.PointAt(10000.0f), 0);
		Polyline.Insert(Polyline[0] + 1000 * PerpSign * UE::Geometry::PerpCW(StartDirOut), 0);

		Polyline.Add(EndLine.PointAt(10000.0f));
		Polyline.Add(Polyline.Last() + 1000 * PerpSign * UE::Geometry::PerpCW(EndDirOut));
		FVector2f StartPos = Polyline[0];
		Polyline.Add(StartPos);		// close polyline (cannot use Polyline[0] in case Add resizes!)
	}

	N = Polyline.Num();

	// project each mesh vertex to view plane and evaluate winding integral of polyline
	const FDynamicMesh3* Mesh = GetSculptMesh();
	TempROIBuffer.SetNum(Mesh->MaxVertexID());
	ParallelFor(Mesh->MaxVertexID(), [&](int32 VertexIdx)
		{
			if (Mesh->IsVertex(VertexIdx))
			{
				FVector3d WorldPos = CurTargetTransform.TransformPosition(Mesh->GetVertex(VertexIdx));
				FVector2f PlanePos = (FVector2f)Lasso.GetProjectedPoint((FVector)WorldPos);

				double WindingSum = 0;
				FVector2f a = Polyline[0] - PlanePos, b = FVector2f::Zero();
				for (int32 i = 1; i < N; ++i)
				{
					b = Polyline[i] - PlanePos;
					WindingSum += (double)FMathf::Atan2(a.X * b.Y - a.Y * b.X, a.X * b.X + a.Y * b.Y);
					a = b;
				}
				WindingSum /= FMathd::TwoPi;
				bool bInside = FMathd::Abs(WindingSum) > 0.3;
				TempROIBuffer[VertexIdx] = bInside ? 1 : 0;
			}
			else
			{
				TempROIBuffer[VertexIdx] = -1;
			}
		});

	// convert to vertex selection, and then select fully-enclosed faces
	UE::Geometry::FMeshVertexSelection VertexSelection(Mesh);
	VertexSelection.SelectByVertexID([&](int32 VertexIdx) { return TempROIBuffer[VertexIdx] == 1; });

	const double SetWeightValue = ToolProperties? ToolProperties->BrushProperties.AttributeValue: 0.0;
	SetVerticesToWeightMap(VertexSelection.AsSet(), SetWeightValue);
}


void UDataflowEditorVertexAttributePaintTool::ComputeGradient()
{
	//if (!ensure(ActiveWeightMap))
	//{
	//	UE_LOG(LogTemp, Warning, TEXT("No active weight map"));
	//	return;
	//}

	//BeginChange();

	//const FDynamicMesh3* const Mesh = DynamicMeshComponent->GetMesh();
	//TempROIBuffer.SetNum(0, EAllowShrinking::No);
	//for (int32 VertexIdx : Mesh->VertexIndicesItr())
	//{
	//	TempROIBuffer.Add(VertexIdx);
	//}

	//if (bHaveDynamicMeshToWeightConversion)
	//{
	//	for (int32 VertexIdx : TempROIBuffer)
	//	{
	//		for (const int32 Idx : WeightToDynamicMesh[DynamicMeshToWeight[VertexIdx]])
	//		{
	//			ActiveWeightEditChangeTracker->SaveVertexOneRingTriangles(Idx, true);
	//		}
	//	}
	//}
	//else
	//{
	//	for (int32 VertexIdx : TempROIBuffer)
	//	{
	//		ActiveWeightEditChangeTracker->SaveVertexOneRingTriangles(VertexIdx, true);
	//	}
	//}


	//for (const int32 VertexIndex : TempROIBuffer)
	//{
	//	const FVector3d Vert = Mesh->GetVertex(VertexIndex);

	//	// (Copied from FClothPaintTool_Gradient::ApplyGradient)

	//	// Get distances
	//	// TODO: Look into surface distance instead of 3D distance? May be necessary for some complex shapes
	//	float DistanceToLowSq = MAX_flt;
	//	for (const int32& LowIndex : LowValueGradientVertexSelection.SelectedCornerIDs)
	//	{
	//		const FVector3d LowPoint = Mesh->GetVertex(LowIndex);
	//		const float DistanceSq = (LowPoint - Vert).SizeSquared();
	//		if (DistanceSq < DistanceToLowSq)
	//		{
	//			DistanceToLowSq = DistanceSq;
	//		}
	//	}

	//	float DistanceToHighSq = MAX_flt;
	//	for (const int32& HighIndex : HighValueGradientVertexSelection.SelectedCornerIDs)
	//	{
	//		const FVector3d HighPoint = Mesh->GetVertex(HighIndex);
	//		const float DistanceSq = (HighPoint - Vert).SizeSquared();
	//		if (DistanceSq < DistanceToHighSq)
	//		{
	//			DistanceToHighSq = DistanceSq;
	//		}
	//	}

	//	const float Value = FMath::LerpStable(FilterProperties->GradientLowValue, FilterProperties->GradientHighValue, DistanceToLowSq / (DistanceToLowSq + DistanceToHighSq));
	//	if (bHaveDynamicMeshToWeightConversion)
	//	{
	//		for (const int32 Idx : WeightToDynamicMesh[DynamicMeshToWeight[VertexIndex]])
	//		{
	//			ActiveWeightMap->SetValue(Idx, &Value);
	//		}
	//	}
	//	else
	//	{
	//		ActiveWeightMap->SetValue(VertexIndex, &Value);
	//	}
	//}

	//// update colors
	//UpdateVertexColorOverlay();
	//DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);
	//GetToolManager()->PostInvalidation();
	//EndChange();

}

bool UDataflowEditorVertexAttributePaintTool::HasSelection() const
{
	if (MeshSelector)
	{
		return MeshSelector->IsAnyComponentSelected();
	}
	return false;
}

void UDataflowEditorVertexAttributePaintTool::GrowSelection() const
{
	if (MeshSelector)
	{
		MeshSelector->GrowSelection();
	}
}

void UDataflowEditorVertexAttributePaintTool::ShrinkSelection() const
{
	if (MeshSelector)
	{
		MeshSelector->ShrinkSelection();
	}
}

void UDataflowEditorVertexAttributePaintTool::FloodSelection() const
{
	if (MeshSelector)
	{
		MeshSelector->FloodSelection();
	}
}

void UDataflowEditorVertexAttributePaintTool::SelectBorder() const
{
	if (MeshSelector)
	{
		MeshSelector->SelectBorder();
	}
}



void UDataflowEditorVertexAttributePaintTool::SetComponentSelectionMode(EComponentSelectionMode NewMode)
{
	if (ToolProperties)
	{
		ToolProperties->SelectionProperties.ComponentSelectionMode = NewMode;
		if (MeshSelector)
		{
			MeshSelector->SetComponentSelectionMode(NewMode);
		}
	}
}

TArray<int32> UDataflowEditorVertexAttributePaintTool::GetSelectedVertices() const
{
	if (MeshSelector)
	{
		return MeshSelector->GetSelectedVertices();
	}
	return {};
}

void UDataflowEditorVertexAttributePaintTool::ApplyValueToSelection(EDataflowEditorToolEditOperation Operation, float InValue)
{
	if (!VertexData.IsValid())
	{
		return;
	}

	VertexROI = UDataflowEditorVertexAttributePaintTool::GetSelectedVertices();
	if (VertexROI.IsEmpty())
	{
		return;
	}

	BeginChange();

	if (FDynamicMesh3* Mesh = GetSculptMesh())
	{
		ROIWeightValueBuffer.SetNum(VertexROI.Num(), EAllowShrinking::No);
		SyncWeightBufferWithMesh(Mesh);

		FDataflowVertexAttributePaintBrushOp::ApplyStampByVerticesStatic(
			Mesh, CurrentStamp, VertexROI, ROIWeightValueBuffer, 
			Operation, InValue, /*bApplyRadiusLimit*/false);

		SyncMeshWithWeightBuffer(Mesh);
	}

	// update colors
	UpdateVertexColorOverlay();
	GetToolManager()->PostInvalidation();
	EndChange();
}

void UDataflowEditorVertexAttributePaintTool::PruneSelection(float Threshold)
{
	if (!VertexData.IsValid())
	{
		return;
	}

	TArray<int32> SelectVertices = UDataflowEditorVertexAttributePaintTool::GetSelectedVertices();
	if (SelectVertices.IsEmpty())
	{
		return;
	}

	BeginChange();

	auto PruneAttributeValue =
		[Threshold](UE::Geometry::FDynamicMeshWeightAttribute& AttributeMap, int32 AttributeIndex) -> double
		{
			float CurrentValue;
			AttributeMap.GetValue(AttributeIndex, &CurrentValue);
			return (CurrentValue >= Threshold) ? CurrentValue : 0;
		};

	for (int32 VertexIdx : SelectVertices)
	{
		VertexData.SetValue(VertexIdx, PruneAttributeValue);
	}

	UpdateVertexColorOverlay();
	GetToolManager()->PostInvalidation();
	EndChange();
}

namespace UE::DataflowEditorVertexAttributePaintTool::Private
{
	const FString CopyAverageFromSelectionToClipboardIdentifier = TEXT("UE_DataflowEditorVertexAttributePaintTool_AverageValue:");
}

void UDataflowEditorVertexAttributePaintTool::CopyAverageFromSelectionToClipboard()
{
	using namespace UE::DataflowEditorVertexAttributePaintTool::Private;

	TArray<int32> SelectVertices = GetSelectedVertices();
	if (SelectVertices.IsEmpty())
	{
		const FText NotificationText = FText::FromString(TEXT("No vertices were selected. Nothing was copied to the clipboard."));
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	const float AverageValue = VertexData.GetAverageValue(SelectVertices);

	// copy to clipboard
	const FString ClipboardString = FString::Format(TEXT("{0}{1}"), { CopyAverageFromSelectionToClipboardIdentifier, FString::SanitizeFloat(AverageValue) });
	FPlatformApplicationMisc::ClipboardCopy(*ClipboardString);

	// notify user
	const FText NotificationText = FText::FromString("Selection average value copied to clipboard.");
	ShowEditorMessage(ELogVerbosity::Log, NotificationText);
}

void UDataflowEditorVertexAttributePaintTool::PasteValueToSelectionFromClipboard()
{
	using namespace UE::DataflowEditorVertexAttributePaintTool::Private;

	if (!HasSelection())
	{
		const FText NotificationText = FText::FromString("No vertices were selected. Nothing was pasted.");
		ShowEditorMessage(ELogVerbosity::Error, NotificationText);
		return;
	}

	// get the clipboard content and check if it matches our format
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	if (!ClipboardContent.StartsWith(CopyAverageFromSelectionToClipboardIdentifier))
	{
		const FText NotificationText = FText::FromString("Failed to paste value from clipboard. Format is incompatible.");
		ShowEditorMessage(ELogVerbosity::Fatal, NotificationText);
		return;
	}

	// parse the value from the clipboard string
	const FString StringValue = ClipboardContent.RightChop(CopyAverageFromSelectionToClipboardIdentifier.Len());
	float ValueToPaste = 0.0f;
	if (StringValue.IsNumeric())
	{
		LexFromString(ValueToPaste, *StringValue);
		ValueToPaste = FMath::Clamp(ValueToPaste, 0.0f, 1.0f);

		ApplyValueToSelection(EDataflowEditorToolEditOperation::Replace, ValueToPaste);
	}
	else
	{
		const FText NotificationText = FText::FromString("Failed to paste value from clipboard, Value from the clipboard is not numeric.");
		ShowEditorMessage(ELogVerbosity::Warning, NotificationText);
	}
	
	// notify user
	const FText NotificationText = FText::FromString("Pasted weights.");
	ShowEditorMessage(ELogVerbosity::Log, NotificationText);
}

void UDataflowEditorVertexAttributePaintTool::OnSelectionModified()
{
	//const bool bToolTypeIsGradient = (FilterProperties->SubToolType == EDataflowEditorWeightMapPaintInteractionType::Gradient);
	//if (bToolTypeIsGradient && MeshSelector)
	//{
	//	const FGroupTopologySelection NewSelection = MeshSelector->GetSelectionMechanic()->GetActiveSelection(); // TODO(ccaillaud) : remove the new to access the mechanic object

	//	const bool bSelectingLowValueGradientVertices = GetCtrlToggle();
	//	if (bSelectingLowValueGradientVertices)
	//	{
	//		LowValueGradientVertexSelection = NewSelection;
	//	}
	//	else
	//	{
	//		HighValueGradientVertexSelection = NewSelection;
	//	}

	//	if (LowValueGradientVertexSelection.SelectedCornerIDs.Num() > 0 && HighValueGradientVertexSelection.SelectedCornerIDs.Num() > 0)
	//	{
	//		ComputeGradient();
	//	}

	//	constexpr bool bBroadcast = false;
	//	MeshSelector->GetSelectionMechanic()->SetSelection(FGroupTopologySelection(), bBroadcast); // TODO(ccaillaud) should probably have a clear selection 
	//}
}


void UDataflowEditorVertexAttributePaintTool::SetVerticesToWeightMap(const TSet<int32>& Vertices, double WeightValue)
{
	BeginChange();

	TempROIBuffer.SetNum(0, EAllowShrinking::No);
	for (int32 VertexIdx : Vertices)
	{
		TempROIBuffer.Add(VertexIdx);
	}

	if (HaveVisibilityFilter())
	{
		TArray<int32> VisibleVertices;
		VisibleVertices.Reserve(TempROIBuffer.Num());
		ApplyVisibilityFilter(TempROIBuffer, VisibleVertices);
		TempROIBuffer = MoveTemp(VisibleVertices);
	}

	auto UpdateAttributeValue =
		[WeightValue](UE::Geometry::FDynamicMeshWeightAttribute& AttributeMap, int32 AttributeIndex) -> double
		{
			return WeightValue;
		};

	for (int32 VertexIdx : TempROIBuffer)
	{
		VertexData.SetValue(VertexIdx, UpdateAttributeValue);
	}

	// update colors
	UpdateVertexColorOverlay();
	//DynamicMeshComponent->FastNotifyVertexAttributesUpdated(EMeshRenderAttributeFlags::VertexColors);
	GetToolManager()->PostInvalidation();

	EndChange();

}

bool UDataflowEditorVertexAttributePaintTool::HaveVisibilityFilter() const
{
	return (ToolProperties && ToolProperties->BrushProperties.VisibilityFilter != EDataflowEditorToolVisibilityType::None);
}

void UDataflowEditorVertexAttributePaintTool::ApplyVisibilityFilter(TSet<int32>& Vertices, TArray<int32>& ROIBuffer, TArray<int32>& OutputBuffer)
{
	ROIBuffer.SetNum(0, EAllowShrinking::No);
	ROIBuffer.Reserve(Vertices.Num());
	for (int32 VertexIdx : Vertices)
	{
		ROIBuffer.Add(VertexIdx);
	}

	OutputBuffer.Reset();
	ApplyVisibilityFilter(TempROIBuffer, OutputBuffer);

	Vertices.Reset();
	for (int32 VertexIdx : OutputBuffer)
	{
		Vertices.Add(VertexIdx);
	}
}

void UDataflowEditorVertexAttributePaintTool::ApplyVisibilityFilter(const TArray<int32>& Vertices, TArray<int32>& VisibleVertices)
{
	if (!HaveVisibilityFilter())
	{
		VisibleVertices = Vertices;
		return;
	}

	FViewCameraState StateOut;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
	FTransform3d LocalToWorld = UE::ToolTarget::GetLocalToWorldTransform(Target);
	FVector3d LocalEyePosition(LocalToWorld.InverseTransformPosition(StateOut.Position));

	const FDynamicMesh3* Mesh = GetSculptMesh();

	int32 NumVertices = Vertices.Num();

	VisibilityFilterBuffer.SetNum(NumVertices, EAllowShrinking::No);
	ParallelFor(NumVertices, [&](int32 idx)
		{
			VisibilityFilterBuffer[idx] = true;
			UE::Geometry::FVertexInfo VertexInfo;
			Mesh->GetVertex(Vertices[idx], VertexInfo, true, false, false);
			FVector3d Centroid = VertexInfo.Position;
			FVector3d FaceNormal = (FVector3d)VertexInfo.Normal;
			if (FaceNormal.Dot((Centroid - LocalEyePosition)) > 0)
			{
				VisibilityFilterBuffer[idx] = false;
			}
			if (ToolProperties->BrushProperties.VisibilityFilter == EDataflowEditorToolVisibilityType::Unoccluded)
			{
				int32 HitTID = Octree.FindNearestHitObject(FRay3d(LocalEyePosition, UE::Geometry::Normalized(Centroid - LocalEyePosition)));
				if (HitTID != IndexConstants::InvalidID && Mesh->IsTriangle(HitTID))
				{
					// Check to see if our vertex has been occulded by another triangle.
					UE::Geometry::FIndex3i TriVertices = Mesh->GetTriangle(HitTID);
					if (TriVertices[0] != Vertices[idx] && TriVertices[1] != Vertices[idx] && TriVertices[2] != Vertices[idx])
					{
						VisibilityFilterBuffer[idx] = false;
					}
				}
			}
		});

	VisibleVertices.Reset();
	for (int32 k = 0; k < NumVertices; ++k)
	{
		if (VisibilityFilterBuffer[k])
		{
			VisibleVertices.Add(Vertices[k]);
		}
	}
}



int32 UDataflowEditorVertexAttributePaintTool::FindHitSculptMeshTriangleConst(const FRay3d& LocalRay) const
{
	if (!IsInBrushMode())
	{
		return IndexConstants::InvalidID;
	}

	if (GetBrushCanHitBackFaces())
	{
		return Octree.FindNearestHitObject(LocalRay);
	}
	else
	{
		const FDynamicMesh3* Mesh = GetSculptMesh();

		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(CurTargetTransform.InverseTransformPosition((FVector3d)StateOut.Position));
		int HitTID = Octree.FindNearestHitObject(LocalRay,
			[this, Mesh, &LocalEyePosition](int TriangleID) {
				FVector3d Normal, Centroid;
				double Area;
				Mesh->GetTriInfo(TriangleID, Normal, Area, Centroid);
				return Normal.Dot((Centroid - LocalEyePosition)) < 0;
			});
		return HitTID;
	}
}

int32 UDataflowEditorVertexAttributePaintTool::FindHitTargetMeshTriangleConst(const FRay3d& LocalRay) const
{
	check(false);
	return IndexConstants::InvalidID;
}

void UDataflowEditorVertexAttributePaintTool::UpdateHitSculptMeshTriangle(int32 TriangleID, const FRay3d& LocalRay)
{
	// TODO: Figure out what the actual position on the triangle is when hit.
	CurrentBaryCentricCoords = FVector3d(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);
}

bool UDataflowEditorVertexAttributePaintTool::UpdateBrushPosition(const FRay& WorldRay)
{
	TUniquePtr<FMeshSculptBrushOp>& UseBrushOp = GetActiveBrushOp();

	bool bHit = false;
	ESculptBrushOpTargetType TargetType = UseBrushOp->GetBrushTargetType();
	switch (TargetType)
	{
	case ESculptBrushOpTargetType::SculptMesh:
	case ESculptBrushOpTargetType::TargetMesh:
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	case ESculptBrushOpTargetType::ActivePlane:
		check(false);
		bHit = UpdateBrushPositionOnSculptMesh(WorldRay, false);
		break;
	}

	if (bHit && (UseBrushOp->GetAlignStampToView() || IsVolumetricBrush()))
	{
		AlignBrushToView();
	}

	return bHit;
}

bool UDataflowEditorVertexAttributePaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (ensure(InStroke() == false))
	{
		UpdateBrushPosition(DevicePos.WorldRay);
	}
	return true;
}


void UDataflowEditorVertexAttributePaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	if (PolyLassoMechanic)
	{
		PolyLassoMechanic->LineColor = FLinearColor::Green;
		PolyLassoMechanic->DrawHUD(Canvas, RenderAPI);
	}

	if (ToolProperties->EditingMode == EDataflowEditorToolEditMode::Mesh)
	{
		if (MeshSelector)
		{
			MeshSelector->DrawHUD(Canvas, RenderAPI);
		}
	}

	if (BrushEditBehavior.IsValid())
	{
		BrushEditBehavior->DrawHUD(Canvas, RenderAPI);
	}

	const bool bInteractiveBrushIsAdjusting = (BrushEditBehavior.IsValid() && BrushEditBehavior->IsEditing());

	// when the user adjust the brush interactively we hide the value on brush
	if (!bInteractiveBrushIsAdjusting && IsInBrushMode())
	{
		if (Canvas && RenderAPI && BrushIndicator)
		{
			// Display the value under the brush
			const float BrushRadius = BrushIndicator->BrushRadius;
			FVector Offset{ 0 };
			const FVector HoverPosition = HoverStamp.WorldFrame.ToFTransform().GetTranslation();

			const FVector BrushPos = HoverPosition + Offset;

			FVector4 ScreenBrushPos = RenderAPI->GetSceneView()->WorldToScreen(BrushPos);
			FVector2D ScreenBrushPos2D = FVector2D(ScreenBrushPos.X, ScreenBrushPos.Y);

			RenderAPI->GetSceneView()->ScreenToPixel(ScreenBrushPos, ScreenBrushPos2D);

			// Render value under at brush even in mesh mode
			const FText BaseText = LOCTEXT("DataflowWeightMapPaintTool_ValueAtBrush_TextFormat", "Current Value: {0}");
			const FText ValueAtBrushText = FText::Format(BaseText, FText::AsNumber(ToolProperties->BrushProperties.ValueAtBrush));
			FCanvasTextItem TextItem(ScreenBrushPos2D, ValueAtBrushText, GEngine->GetLargeFont(), FLinearColor::White);
			TextItem.EnableShadow(FLinearColor::Black);
			TextItem.bCentreX = true;
			Canvas->DrawItem(TextItem);
		}
	}
}

void UDataflowEditorVertexAttributePaintTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UMeshSculptToolBase::Render(RenderAPI);

	if (ToolProperties->EditingMode == EDataflowEditorToolEditMode::Mesh)
	{
		if (MeshSelector)
		{
			MeshSelector->Render(RenderAPI);
		}
	}
}


void UDataflowEditorVertexAttributePaintTool::UpdateMaterialMode(EMeshEditingMaterialModes MaterialMode)
{
	if (MaterialMode == EMeshEditingMaterialModes::VertexColor)
	{
		constexpr bool bUseTwoSidedMaterial = true;
		ActiveOverrideMaterial = ToolSetupUtil::GetVertexColorMaterial(GetToolManager(), bUseTwoSidedMaterial);
		if (ensure(ActiveOverrideMaterial != nullptr))
		{
			GetSculptMeshComponent()->SetOverrideRenderMaterial(ActiveOverrideMaterial);
			ActiveOverrideMaterial->SetScalarParameterValue(TEXT("FlatShading"), (ViewProperties->bFlatShading) ? 1.0f : 0.0f);
		}
		GetSculptMeshComponent()->SetShadowsEnabled(false);
	}
	else
	{
		UMeshSculptToolBase::UpdateMaterialMode(MaterialMode);
	}
}

void UDataflowEditorVertexAttributePaintTool::OnTick(float DeltaTime)
{
	UMeshSculptToolBase::OnTick(DeltaTime);
	MeshElementsDisplay->OnTick(DeltaTime);

	bool bIsLasso = false; // TODO(ccaillaud) const bool bIsLasso = (FilterProperties->SubToolType == EDataflowEditorWeightMapPaintInteractionType::PolyLasso);
	if (PolyLassoMechanic)
	{
		PolyLassoMechanic->SetIsEnabled(bIsLasso);
	}

	const bool bIsMeshEditMode = (ToolProperties->EditingMode == EDataflowEditorToolEditMode::Mesh);
	if (MeshSelector)
	{
		MeshSelector->SetIsEnabled(bIsMeshEditMode);
	}

	ConfigureIndicator(IsVolumetricBrush());
	SetIndicatorVisibility(!bIsLasso && !bIsMeshEditMode);

	SCOPE_CYCLE_COUNTER(VertexAttributePaintTool_Tick);

	// process the undo update
	if (bUndoUpdatePending)
	{
		// wait for updates
		WaitForPendingUndoRedoUpdate();

		// post rendering update
		//DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(AccumulatedTriangleROI, EMeshRenderAttributeFlags::VertexColors);
		GetToolManager()->PostInvalidation();

		// ignore stamp and wait for next tick to do anything else
		bUndoUpdatePending = false;
		return;
	}

	// Get value at brush location
	const bool bShouldPickWeight = bPendingPickWeight && IsStampPending() == false;
//	const bool bShouldUpdateValueAtBrush = IsInBrushSubMode();

	//if (bShouldPickWeight /*|| bShouldUpdateValueAtBrush*/)
	{
		if (GetSculptMesh()->IsTriangle(GetBrushTriangleID()))
		{
			const double HitWeightValue = GetCurrentWeightValueUnderBrush();

			if (bShouldPickWeight)
			{
				ToolProperties->BrushProperties.AttributeValue = HitWeightValue;
				NotifyOfPropertyChangeByTool(ToolProperties);
			}

			/*if (bShouldUpdateValueAtBrush)*/
			{
				ToolProperties->BrushProperties.ValueAtBrush = HitWeightValue;
			}
		}
		bPendingPickWeight = false;
	}

	auto ExecuteStampOperation = [this](int StampIndex, const FRay& StampRay)
		{
			SCOPE_CYCLE_COUNTER(VertexAttributePaintTool_Tick_ApplyStampBlock);

			// update sculpt ROI
			UpdateROI(CurrentStamp);

			// append updated ROI to modified region (async)
			FDynamicMesh3* Mesh = GetSculptMesh();
			TFuture<void> AccumulateROI = Async(UE::DataflowEditorVertexAttributePaintTool::Private::WeightPaintToolAsyncExecTarget, [&]()
				{
					UE::Geometry::VertexToTriangleOneRing(Mesh, VertexROI, AccumulatedTriangleROI);
				});

			// apply the stamp
			bool bWeightsModified = ApplyStamp();

			if (bWeightsModified)
			{
				SCOPE_CYCLE_COUNTER(VertexAttributePaintTool_Tick_UpdateMeshBlock);
				UpdateVertexColorOverlay(&TriangleROI);
				//DynamicMeshComponent->FastNotifyTriangleVerticesUpdated(TriangleROI, EMeshRenderAttributeFlags::VertexColors);
				GetToolManager()->PostInvalidation();
			}

			// we don't really need to wait for these to happen to end Tick()...
			AccumulateROI.Wait();
		};


	if (IsInBrushMode())
	{
		ProcessPerTickStamps(
			[this](const FRay& StampRay) -> bool {
				return UpdateStampPosition(StampRay);
			}, ExecuteStampOperation);
	}

}

bool UDataflowEditorVertexAttributePaintTool::CanAccept() const
{
	return bAnyChangeMade;
}

void UDataflowEditorVertexAttributePaintTool::BeginChange()
{
	VertexData.BeginChange(Cast<UDynamicMeshComponent>(GetSculptMeshComponent()));
	LongTransactions.Open(LOCTEXT("WeightPaintChange", "Weight Stroke"), GetToolManager());
}

void UDataflowEditorVertexAttributePaintTool::EndChange()
{
	bAnyChangeMade = true;

	using namespace UE::DataflowEditorVertexAttributePaintTool;

	TUniquePtr<Private::FMeshChange> MeshChange = VertexData.EndChange(Cast<UDynamicMeshComponent>(GetSculptMeshComponent()));

	TUniquePtr<TWrappedToolCommandChange<Private::FMeshChange>> NewChange = MakeUnique<TWrappedToolCommandChange<Private::FMeshChange>>();
	NewChange->WrappedChange = MoveTemp(MeshChange);
	NewChange->BeforeModify = [this](bool bRevert)
		{
			this->WaitForPendingUndoRedoUpdate();
		};

	GetToolManager()->EmitObjectChange(GetSculptMeshComponent(), MoveTemp(NewChange), LOCTEXT("WeightPaintChange", "Weight Stroke"));
	LongTransactions.Close(GetToolManager());
}

void UDataflowEditorVertexAttributePaintTool::CancelChange()
{
	VertexData.CancelChange();
	LongTransactions.Close(GetToolManager());
}


void UDataflowEditorVertexAttributePaintTool::WaitForPendingUndoRedoUpdate()
{
	if (bUndoUpdatePending)
	{
		bUndoUpdatePending = false;
	}
}

void UDataflowEditorVertexAttributePaintTool::OnDynamicMeshComponentChanged(UDynamicMeshComponent* Component, const FMeshVertexChange* Change, bool bRevert)
{
	// update octree
	FDynamicMesh3* Mesh = GetSculptMesh();

	// make sure any previous async computations are done, and update the undo ROI
	if (bUndoUpdatePending)
	{
		// we should never hit this anymore, because of pre-change calling WaitForPendingUndoRedoUpdate()
		WaitForPendingUndoRedoUpdate();

		// this is not right because now we are going to do extra recomputation, but it's very messy otherwise...
		UE::Geometry::VertexToTriangleOneRing(Mesh, Change->Vertices, AccumulatedTriangleROI);
	}
	else
	{
		AccumulatedTriangleROI.Reset();
		UE::Geometry::VertexToTriangleOneRing(Mesh, Change->Vertices, AccumulatedTriangleROI);
	}

	// note that we have a pending update
	bUndoUpdatePending = true;
}


void UDataflowEditorVertexAttributePaintTool::PrecomputeFilterData()
{
	const FDynamicMesh3* Mesh = GetSculptMesh();

	TriNormals.SetNum(Mesh->MaxTriangleID());
	ParallelFor(Mesh->MaxTriangleID(), [&](int32 tid)
		{
			if (Mesh->IsTriangle(tid))
			{
				TriNormals[tid] = Mesh->GetTriNormal(tid);
			}
		});

	const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();
	const UE::Geometry::FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->PrimaryUV();
	UVSeamEdges.SetNum(Mesh->MaxEdgeID());
	NormalSeamEdges.SetNum(Mesh->MaxEdgeID());
	ParallelFor(Mesh->MaxEdgeID(), [&](int32 eid)
		{
			if (Mesh->IsEdge(eid))
			{
				UVSeamEdges[eid] = UVs->IsSeamEdge(eid);
				NormalSeamEdges[eid] = Normals->IsSeamEdge(eid);
			}
		});
}

float UDataflowEditorVertexAttributePaintTool::GetCurrentWeightValueUnderBrush() const
{
	const int32 VertexID = GetBrushNearestVertex();
	return VertexData.GetValue(VertexID);
}

int32 UDataflowEditorVertexAttributePaintTool::GetBrushNearestVertex() const
{
	int TriangleVertex = 0;

	if (CurrentBaryCentricCoords.X >= CurrentBaryCentricCoords.Y && CurrentBaryCentricCoords.X >= CurrentBaryCentricCoords.Z)
	{
		TriangleVertex = 0;
	}
	else
	{
		if (CurrentBaryCentricCoords.Y >= CurrentBaryCentricCoords.X && CurrentBaryCentricCoords.Y >= CurrentBaryCentricCoords.Z)
		{
			TriangleVertex = 1;
		}
		else
		{
			TriangleVertex = 2;
		}
	}
	const FDynamicMesh3* Mesh = GetSculptMesh();

	int32 Tid = GetBrushTriangleID();
	if (Tid == IndexConstants::InvalidID)
	{
		return IndexConstants::InvalidID;
	}

	UE::Geometry::FIndex3i Vertices = Mesh->GetTriangle(Tid);
	return Vertices[TriangleVertex];
}

void UDataflowEditorVertexAttributePaintTool::UpdateBrushType(EDataflowEditorToolEditOperation BrushMode)
{
	static const FText BaseMessage = LOCTEXT("OnStartTool", "Hold [Shift] to Relax values, Hold [Ctrl] to Invert Value, [/] and S/D change Size (+Shift to small-step)");
	FTextBuilder Builder;
	Builder.AppendLine(BaseMessage);

	if (ToolProperties)
	{
		ToolProperties->BrushProperties.BrushMode = BrushMode;
	}
	SetActivePrimaryBrushType(PaintBrushId);
	SetActiveSecondaryBrushType(SmoothBrushId);

	SetToolPropertySourceEnabled(PaintBrushOpProperties, false);
	SetToolPropertySourceEnabled(SmoothBrushOpProperties, false);
	SetToolPropertySourceEnabled(GizmoProperties, false);

	GetToolManager()->DisplayMessage(Builder.ToText(), EToolMessageLevel::UserNotification);
}

void UDataflowEditorVertexAttributePaintTool::OnColorRampChanged(TArray<FRichCurve*> Curves)
{
	if (ToolProperties)
	{
		ToolProperties->SaveConfig();
	}
	UpdateVertexColorOverlay();
	GetToolManager()->PostInvalidation();
}

void UDataflowEditorVertexAttributePaintTool::SetColorMode(EDataflowEditorToolColorMode NewColorMode)
{
	if (ToolProperties)
	{
		ToolProperties->DisplayProperties.ColorMode = NewColorMode;
		ToolProperties->SaveConfig();
		UpdateVertexColorOverlay();
		GetToolManager()->PostInvalidation();

		FPropertyChangedEvent ColorModePropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDataflowEditorVertexAttributePaintToolDisplayProperties, ColorMode)));
		ToolProperties->PostEditChangeProperty(ColorModePropertyChangedEvent);
	}
}

void UDataflowEditorVertexAttributePaintTool::UpdateVertexColorOverlay(const TSet<int>* TrianglesToUpdate)
{
	auto ValueToColor = [this](float Value) -> FVector4f
		{
			switch (ToolProperties->DisplayProperties.ColorMode)
			{
			case EDataflowEditorToolColorMode::Greyscale:
				return FMath::Lerp(FLinearColor::Black, FLinearColor::White, Value);
			case EDataflowEditorToolColorMode::Ramp:
				return ToolProperties->DisplayProperties.ColorRamp.ColorRamp.GetLinearColorValue(Value);
			case EDataflowEditorToolColorMode::FullMaterial:
				return FLinearColor::White;
			}
			return FLinearColor::White;
		};

	auto SetColorsFromWeights = [&](FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshColorOverlay& ColorOverlay, int TriangleID)
		{
			const UE::Geometry::FIndex3i Tri = Mesh.GetTriangle(TriangleID);
			const UE::Geometry::FIndex3i ColorElementTri = ColorOverlay.GetTriangle(TriangleID);

			for (int TriVertIndex = 0; TriVertIndex < 3; ++TriVertIndex)
			{
				const float Value = VertexData.GetValue(Tri[TriVertIndex]);
				ColorOverlay.SetElement(ColorElementTri[TriVertIndex], ValueToColor(Value));
			}
		};

	// update mesh with new value colors
	PreviewMesh->DeferredEditMesh(
		[this, &TrianglesToUpdate, &SetColorsFromWeights](FDynamicMesh3& Mesh)
		{
			check(Mesh.HasAttributes());
			check(Mesh.Attributes()->PrimaryColors());
			
			if (UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = Mesh.Attributes()->PrimaryColors())
			{
				if (TrianglesToUpdate)
				{
					for (const int TriangleID : *TrianglesToUpdate)
					{
						SetColorsFromWeights(Mesh, *ColorOverlay, TriangleID);
					}
				}
				else
				{
					for (const int TriangleID : Mesh.TriangleIndicesItr())
					{
						SetColorsFromWeights(Mesh, *ColorOverlay, TriangleID);
					}
				}
			}
		}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}

void UDataflowEditorVertexAttributePaintTool::MirrorValues()
{
	if (ToolProperties)
	{
		if (!VertexData.IsValid())
		{
			return;
		}

		VertexROI = UDataflowEditorVertexAttributePaintTool::GetSelectedVertices();
		if (VertexROI.IsEmpty())
		{
			return;
		}

		BeginChange();

		if (FDynamicMesh3* Mesh = GetSculptMesh())
		{
			MirrorData.EnsureMirrorDataIsUpdated(*Mesh, ToolProperties->MirrorProperties.MirrorAxis, ToolProperties->MirrorProperties.MirrorDirection);

			TArray<int32> VerticesToUpdate;
			MirrorData.FindMirroredIndices(*Mesh, VertexROI, VerticesToUpdate);

			// get the selected vertices values
			ROIWeightValueBuffer.SetNum(VertexROI.Num(), EAllowShrinking::No);
			SyncWeightBufferWithMesh(Mesh);

			// now swap VertexROI with VerticesToUpdate ( ROIWeightValueBuffer map 1:1 to VerticesToUpdate)
			Swap(VertexROI, VerticesToUpdate);

			// apply the values back to the mesh with the mirrored indices
			SyncMeshWithWeightBuffer(Mesh);
		}

		// update colors
		UpdateVertexColorOverlay();
		GetToolManager()->PostInvalidation();
		EndChange();

	}
}

void UDataflowEditorVertexAttributePaintTool::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDataflowEditorVertexAttributePaintTool* This = CastChecked<UDataflowEditorVertexAttributePaintTool>(InThis);
	Collector.AddReferencedObject(This->PreviewMesh);
	Collector.AddReferencedObject(This->MeshSelector);
	Collector.AddReferencedObject(This->MeshElementsDisplay);
	Collector.AddReferencedObject(This->DataflowEditorContextObject);
	Super::AddReferencedObjects(InThis, Collector);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 
// MirrorData
// 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool UDataflowEditorVertexAttributePaintTool::FMirrorData::IsPointOnTargetMirrorSide(const FVector& InPoint) const
{
	if (Axis == EAxis::None)
	{
		return false;
	}
	if (Direction == EDataflowEditorToolMirrorDirection::PositiveToNegative && InPoint[Axis - 1] >= 0.f)
	{
		return false; // target is negative side, but point is on positive side
	}
	if (Direction == EDataflowEditorToolMirrorDirection::NegativeToPositive && InPoint[Axis - 1] <= 0.f)
	{
		return false; // target is positive side, but vertex is on negative side
	}
	return true;
}

void UDataflowEditorVertexAttributePaintTool::FMirrorData::EnsureMirrorDataIsUpdated(const FDynamicMesh3& Mesh, EAxis::Type InMirrorAxis, EDataflowEditorToolMirrorDirection InMirrorDirection)
{
	if (VertexMap.Num() > 0 && InMirrorAxis == Axis && InMirrorDirection == Direction)
	{
		// already initialized, just re-use cached data
		return;
	}

	// need to re-initialize
	Axis = InMirrorAxis;
	Direction = InMirrorDirection;
	VertexMap.Reset();

	const int32 NumVertices = Mesh.VertexCount();

	// build a spatial hash grid
	constexpr float HashGridCellSize = 2.0f; // the length of the cell size in the point hash grid
	UE::Geometry::TPointHashGrid3f<int32> VertHash(HashGridCellSize, INDEX_NONE);
	VertHash.Reserve(NumVertices);

	for (int32 VertexID : Mesh.VertexIndicesItr())
	{
		const FVector VertexPos = Mesh.GetVertex(VertexID);
		VertHash.InsertPointUnsafe(VertexID, static_cast<FVector3f>(VertexPos));
	}

	// generate a map of point IDs on the target side, to their equivalent vertex ID on the source side 
	for (int32 TargetVertexID : Mesh.VertexIndicesItr())
	{
		const FVector SourcePosition = Mesh.GetVertex(TargetVertexID);

		// flip position across the mirror axis
		FVector3f MirroredPosition = FVector3f(SourcePosition);
		MirroredPosition[Axis - 1] *= -1.f;

		// Query spatial hash near mirrored position, gradually increasing search radius until at least 1 point is found
		TPair<int32, double> ClosestMirroredPoint = { INDEX_NONE, TNumericLimits<double>::Max() };
		float SearchRadius = HashGridCellSize;
		while (ClosestMirroredPoint.Key == INDEX_NONE)
		{
			ClosestMirroredPoint = VertHash.FindNearestInRadius(
				MirroredPosition,
				SearchRadius,
				[&Mesh, MirroredPosition](int32 VID)
				{
					return FVector3f::DistSquared(FVector3f(Mesh.GetVertex(VID)), MirroredPosition);
				});

			SearchRadius += HashGridCellSize;

			// forcibly break out if search radius gets bigger than the maximum search radius
			static float MaxSearchRadius = 15.f; // TODO we may want to expose this value to the user...
			if (SearchRadius >= MaxSearchRadius)
			{
				break;
			}
		}

		if (ClosestMirroredPoint.Key != INDEX_NONE)
		{
			VertexMap.FindOrAdd(TargetVertexID, ClosestMirroredPoint.Key);
		}
	}
}

void UDataflowEditorVertexAttributePaintTool::FMirrorData::FindMirroredIndices(const FDynamicMesh3& Mesh, const TArray<int32>& SelectedVertices, TArray<int32>& OutVerticesToUpdate)
{
	check(Axis != EAxis::None);

	// results have a 1:1 mapping to the input vertices
	OutVerticesToUpdate.SetNum(SelectedVertices.Num());

	// we need to convert selection to the equivalent target vertex indices (on the target side of the mirror plane)
	// if a vertex is already on the target side, great
	// if the user selected vertices on the source side, we convert them to the mirrored equivalent on the target side
	TSet<VertexIndex> TargetVertices;
	TArray<VertexIndex> MissingVertices;
	for (int32 Index = 0; Index < SelectedVertices.Num(); ++Index)
	{
		const int32 SelectedVertexID = SelectedVertices[Index];
		int32& TargetVertexID = OutVerticesToUpdate[Index];

		// get the mirrored index or the invaliod index if not found
		OutVerticesToUpdate[Index] = VertexMap.FindRef(SelectedVertexID, INDEX_NONE);

		// if the selected vertex is on the wrong side, reset to an invalid index
		if (Mesh.IsVertex(TargetVertexID))
		{
			const FVector TargetPosition = Mesh.GetVertex(TargetVertexID);
			if (IsPointOnTargetMirrorSide(TargetPosition))
			{
				OutVerticesToUpdate[Index] = INDEX_NONE;
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE

