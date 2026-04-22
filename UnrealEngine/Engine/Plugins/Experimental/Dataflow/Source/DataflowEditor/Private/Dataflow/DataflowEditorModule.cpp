// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorModule.h"

#include "Dataflow/DataflowConstructionVisualization.h"
#include "Dataflow/DataflowEditorStyle.h"
#include "Dataflow/DataflowEditorMode.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEngineRendering.h"
#include "Dataflow/DataflowFreezeActionsCustomization.h"
#include "Dataflow/DataflowFunctionProperty.h"
#include "Dataflow/DataflowFunctionPropertyCustomization.h"
#include "Dataflow/DataflowSNodeFactories.h"
#include "Dataflow/DebugDrawText3dVisualization.h"
#include "Dataflow/MeshStatsConstructionVisualization.h"
#include "Dataflow/MeshConstructionVisualization.h"
#include "Dataflow/ScalarVertexPropertyGroupCustomization.h"
#include "Dataflow/DataflowAnyTypeCustomization.h"
#include "Dataflow/DataflowToolRegistry.h"
#include "DataflowEditorTools/DataflowEditorWeightMapPaintTool.h"
#include "DataflowEditorTools/DataflowEditorVertexAttributePaintTool.h"
#include "Dataflow/DataflowCollectionAddScalarVertexPropertyNode.h"
#include "DataflowEditorTools/DataflowEditorVertexAttributePaintToolDetailCustomization.h"

#include "Dataflow/DataflowInstanceDetails.h"

#include "PropertyEditorModule.h"
#include "EditorModeRegistry.h"
#include "Dataflow/DataflowCollectionEditSkinWeightsNode.h"
#include "Dataflow/DataflowCollectionEditSkeletonBonesNode.h"
#include "DataflowEditorTools/DataflowEditorSkinWeightsPaintTool.h"
#include "DataflowEditorTools/DataflowEditorEditSkeletonBonesTool.h"
#include "DataflowEditorTools/DataflowEditorCorrectSkinWeightsNode.h"
#include "Dataflow/DataflowColorRamp.h"
#include "Dataflow/DataflowColorRampCustomization.h"

#include "DataflowRendering/DataflowStaticMeshRenderableType.h"
#include "DataflowRendering/DataflowSkeletalMeshRenderableType.h"
#include "DataflowRendering/DataflowDynamicMeshRenderableType.h"
#include "DataflowRendering/DataflowGeometryCollectionRenderableType.h"
#include "DataflowRendering/DataflowGeometryCollectionProximityRenderableType.h"

#define LOCTEXT_NAMESPACE "DataflowEditor"

namespace UE::Dataflow::Private
{
	static const FName ScalarVertexPropertyGroupName = TEXT("ScalarVertexPropertyGroup");
	static const FName DataflowFunctionPropertyName = TEXT("DataflowFunctionProperty");
	static const FName DataflowVariableOverridesName = TEXT("DataflowVariableOverrides");
	static const FName DataflowFreezeActionsName = TEXT("DataflowFreezeActions");
	static const FName DataflowColorRampName = TEXT("DataflowColorRamp");

	class FDataflowEditorWeightMapPaintToolActionCommands : public TInteractiveToolCommands<FDataflowEditorWeightMapPaintToolActionCommands>
	{
	public:
		FDataflowEditorWeightMapPaintToolActionCommands() : 
			TInteractiveToolCommands<FDataflowEditorWeightMapPaintToolActionCommands>(
				TEXT("DataflowEditorWeightMapPaintToolContext"),
				LOCTEXT("DataflowEditorWeightMapPaintToolContext", "Dataflow Weight Map Paint Tool Context"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{}

		virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override
		{
			ToolCDOs.Add(GetMutableDefault<UDataflowEditorWeightMapPaintTool>());
		}
	};

	class FDataflowEditorSkinWeightPaintToolActionCommands : public TInteractiveToolCommands<FDataflowEditorSkinWeightPaintToolActionCommands>
	{
	public:
		FDataflowEditorSkinWeightPaintToolActionCommands() : 
			TInteractiveToolCommands<FDataflowEditorSkinWeightPaintToolActionCommands>(
				TEXT("DataflowEditorSkinWeightPaintToolContext"),
				LOCTEXT("DataflowEditorSkinWeightPaintToolContext", "Dataflow Skin weight Paint Tool Context"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{}

		virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override
		{
			ToolCDOs.Add(GetMutableDefault<UDataflowEditorSkinWeightsPaintTool>());
		}
	};

	class FDataflowEditorEditSkeletonBonesToolActionCommands : public TInteractiveToolCommands<FDataflowEditorEditSkeletonBonesToolActionCommands>
	{
	public:
		FDataflowEditorEditSkeletonBonesToolActionCommands() : 
			TInteractiveToolCommands<FDataflowEditorEditSkeletonBonesToolActionCommands>(
				TEXT("DataflowEditorEditSkeletonBonesToolContext"),
				LOCTEXT("DataflowEditorEditSkeletonBonesToolContext", "Dataflow skeleton edit Tool Context"),
				NAME_None,
				FAppStyle::GetAppStyleSetName())
		{}

		virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override
		{
			ToolCDOs.Add(GetMutableDefault<UDataflowEditorEditSkeletonBonesTool>());
		}
	};

	class FDataflowToolActionCommandBindings : public UE::Dataflow::FDataflowToolRegistry::IDataflowToolActionCommands
	{
	public:
		FDataflowToolActionCommandBindings()
		{
			FDataflowEditorWeightMapPaintToolActionCommands::Register();
			FDataflowEditorSkinWeightPaintToolActionCommands::Register();
			FDataflowEditorEditSkeletonBonesToolActionCommands::Register();
		}

		virtual void UnbindActiveCommands(const TSharedPtr<FUICommandList>& UICommandList) const override
		{
			checkf(FDataflowEditorWeightMapPaintToolActionCommands::IsRegistered(), TEXT("Expected WeightMapPaintTool actions to have been registered"));
			FDataflowEditorWeightMapPaintToolActionCommands::Get().UnbindActiveCommands(UICommandList);

			checkf(FDataflowEditorSkinWeightPaintToolActionCommands::IsRegistered(), TEXT("Expected SkinWeightPaintTool actions to have been registered"));
			FDataflowEditorSkinWeightPaintToolActionCommands::Get().UnbindActiveCommands(UICommandList);

			checkf(FDataflowEditorEditSkeletonBonesToolActionCommands::IsRegistered(), TEXT("Expected SkeletonEditTool actions to have been registered"));
			FDataflowEditorEditSkeletonBonesToolActionCommands::Get().UnbindActiveCommands(UICommandList);
		}

		virtual void BindCommandsForCurrentTool(const TSharedPtr<FUICommandList>& UICommandList, UInteractiveTool* Tool) const override
		{
			if (ExactCast<UDataflowEditorWeightMapPaintTool>(Tool))
			{
				checkf(FDataflowEditorWeightMapPaintToolActionCommands::IsRegistered(), TEXT("Expected WeightMapPaintTool actions to have been registered"));
				FDataflowEditorWeightMapPaintToolActionCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool);
			}
			if (ExactCast<UDataflowEditorSkinWeightsPaintTool>(Tool))
			{
				checkf(FDataflowEditorSkinWeightPaintToolActionCommands::IsRegistered(), TEXT("Expected SkinWeightPaintTool actions to have been registered"));
				FDataflowEditorSkinWeightPaintToolActionCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool);
			}
			if (ExactCast<UDataflowEditorEditSkeletonBonesTool>(Tool))
			{
				checkf(FDataflowEditorEditSkeletonBonesToolActionCommands::IsRegistered(), TEXT("Expected SkeletonEditTool actions to have been registered"));
				FDataflowEditorEditSkeletonBonesToolActionCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool);
			}
		}
	};
}

const FColor FDataflowEditorModule::SurfaceColor = FLinearColor(0.6, 0.6, 0.6).ToRGBE();

void FDataflowEditorModule::StartupModule()
{
	using namespace UE::Dataflow::Private;

	FDataflowEditorStyle::Get();
	
	// Register type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->RegisterCustomPropertyTypeLayout(ScalarVertexPropertyGroupName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FScalarVertexPropertyGroupCustomization::MakeInstance));
		PropertyModule->RegisterCustomPropertyTypeLayout(DataflowFunctionPropertyName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FFunctionPropertyCustomization::MakeInstance));
		PropertyModule->RegisterCustomPropertyTypeLayout(DataflowVariableOverridesName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDataflowVariableOverridesDetails::MakeInstance));
		PropertyModule->RegisterCustomPropertyTypeLayout(DataflowFreezeActionsName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FFreezeActionsCustomization::MakeInstance));

		PropertyModule->RegisterCustomPropertyTypeLayout(DataflowColorRampName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&UE::Dataflow::FDataflowColorRampCustomization::MakeInstance));
		PropertyModule->RegisterCustomClassLayout(UDataflowEditorVertexAttributePaintToolProperties::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&UE::Dataflow::Editor::FVertexAttributePaintToolDetailCustomization::MakeInstance));

		UE::Dataflow::RegisterAnyTypeCustomizations(*PropertyModule);
	}

	UE::Dataflow::RenderingCallbacks();

	UE::Dataflow::FDataflowToolRegistry& ToolRegistry = UE::Dataflow::FDataflowToolRegistry::Get();
	TSharedRef<const FDataflowToolActionCommandBindings> Actions = MakeShared<FDataflowToolActionCommandBindings>();
	
	ToolRegistry.AddNodeToToolMapping(FDataflowCollectionAddScalarVertexPropertyNode::StaticType(),
		NewObject<UDataflowEditorVertexAttributePaintToolBuilder>(), Actions,
		FSlateIcon(FName("DataflowEditorStyle"), FName("Dataflow.PaintWeightMap")), LOCTEXT("AddWeightMapNodeButtonText", "Paint Weight Map"));

	ToolRegistry.AddNodeToToolMapping(FDataflowCollectionEditSkinWeightsNode::StaticType(), 
		NewObject<UDataflowEditorSkinWeightsPaintToolBuilder>(), Actions,
		FSlateIcon(FName("DataflowEditorStyle"), FName("Dataflow.EditSkinWeights")), LOCTEXT("AddSkinWeightNodeButtonText", "Edit Skin Weights"));

	ToolRegistry.AddNodeToToolMapping(FDataflowCollectionEditSkeletonBonesNode::StaticType(), 
		NewObject<UDataflowEditorEditSkeletonBonesToolBuilder>(), Actions,
		FSlateIcon(FName("DataflowEditorStyle"), FName("Dataflow.EditSkeletonBones")), LOCTEXT("AddSkeletonEditNodeButtonText", "Edit Skeleton Bones"));
		
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowCorrectSkinWeightsNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowGetSkinningSelectionNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FDataflowSetSkinningSelectionNode);
	
	UE::Dataflow::FDataflowConstructionVisualizationRegistry& ConstructionVisualizationRegistry = UE::Dataflow::FDataflowConstructionVisualizationRegistry::GetInstance();
	ConstructionVisualizationRegistry.RegisterVisualization(MakeUnique<UE::Dataflow::FMeshStatsConstructionVisualization>());
	ConstructionVisualizationRegistry.RegisterVisualization(MakeUnique<UE::Dataflow::FMeshConstructionVisualization>());
	ConstructionVisualizationRegistry.RegisterVisualization(MakeUnique<UE::Dataflow::FDebugDrawText3dVisualization>());
	
	FDataflowEditorCommands::Register();

	// register rendering types
	UE::Dataflow::Private::RegisterStaticMeshRenderableTypes();
	UE::Dataflow::Private::RegisterSkeletalMeshRenderableTypes();
	UE::Dataflow::Private::RegisterDynamicMeshRenderableTypes();
	UE::Dataflow::Private::RegisterGeometryCollectionRenderableTypes();
	UE::Dataflow::Private::RegisterGeometryCollectionProximityRenderableTypes();
}

void FDataflowEditorModule::ShutdownModule()
{
	using namespace UE::Dataflow::Private;

	FEditorModeRegistry::Get().UnregisterMode(UDataflowEditorMode::EM_DataflowEditorModeId);

	// Deregister type customizations
	if (FPropertyEditorModule* const PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyModule->UnregisterCustomPropertyTypeLayout(ScalarVertexPropertyGroupName);
		PropertyModule->UnregisterCustomPropertyTypeLayout(DataflowFunctionPropertyName);
		PropertyModule->UnregisterCustomPropertyTypeLayout(DataflowVariableOverridesName);
		PropertyModule->UnregisterCustomPropertyTypeLayout(DataflowFreezeActionsName);
		UE::Dataflow::UnregisterAnyTypeCustomizations(*PropertyModule);
	}

	FDataflowEditorCommands::Unregister();

	UE::Dataflow::FDataflowToolRegistry& ToolRegistry = UE::Dataflow::FDataflowToolRegistry::Get();
	ToolRegistry.RemoveNodeToToolMapping(FDataflowCollectionAddScalarVertexPropertyNode::StaticType());
	ToolRegistry.RemoveNodeToToolMapping(FDataflowCollectionEditSkinWeightsNode::StaticType());
	ToolRegistry.RemoveNodeToToolMapping(FDataflowCollectionEditSkeletonBonesNode::StaticType());

	UE::Dataflow::FDataflowConstructionVisualizationRegistry& ConstructionVisualizationRegistry = UE::Dataflow::FDataflowConstructionVisualizationRegistry::GetInstance();
	ConstructionVisualizationRegistry.DeregisterVisualization(UE::Dataflow::FMeshStatsConstructionVisualization::Name);
	ConstructionVisualizationRegistry.DeregisterVisualization(UE::Dataflow::FMeshConstructionVisualization::Name);
	ConstructionVisualizationRegistry.DeregisterVisualization(UE::Dataflow::FDebugDrawText3dVisualization::Name);
}

IMPLEMENT_MODULE(FDataflowEditorModule, DataflowEditor)


#undef LOCTEXT_NAMESPACE
