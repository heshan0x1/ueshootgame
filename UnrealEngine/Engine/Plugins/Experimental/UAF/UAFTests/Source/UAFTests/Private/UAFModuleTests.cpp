// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR

#include "UAFTestsUtilities.h"
#include "CQTest.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WorkspaceEditor/Private/Workspace.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModuleFactory.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "Math/Vector2D.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "UObject/SavePackage.h"

TEST_CLASS(FAnimNextModuleTests, "Animation.UAF.Functional")
{
	UWorkspace* AnimNextWorkspace = nullptr;
	UAnimNextModule* AnimNextModule = nullptr;
	UPackage* AnimNextWorkspacePackage = nullptr;
	UPackage* AnimNextModulePackage = nullptr;
	FName AnimNextWorkspacePackageName;
	FName AnimNextModulePackageName;
	UAssetEditorSubsystem* AssetEditorSubsystem = nullptr;
	UAnimNextRigVMAssetEditorData* EditorData = nullptr;
	URigVMGraph* ControllerGraph = nullptr;
	UEdGraph* ParentGraph = nullptr;
	UEdGraphNode* RunGraphNode = nullptr;
	UEdGraphNode* WritePoseNode = nullptr;
	const FString InitializeNodeName = "AnimNextInitializeEvent";
	const FString RunGraphNodeName = "AnimNextRunAnimationGraph_v2";
	const FString WritePoseNodeName = "AnimNextWriteSkeletalMeshComponentPose";
	const FString CollapseNodeName = "CollapseNode";
	const FString FunctionNodeName = "New Function";
	const FString VariableName = "NewVariable";
	const FString GetVariableNodeName = "VariableNode";
	const FString SetVariableNodeName = "VariableNode_1";

	BEFORE_EACH()
	{
		AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		ASSERT_THAT(IsNotNull(AssetEditorSubsystem));
	}
	
	AFTER_EACH()
	{
		if (AnimNextWorkspace)
		{
			ObjectTools::DeleteSingleObject(AnimNextWorkspace);
			AnimNextWorkspace = nullptr;
		}
		
		if (AnimNextModule)
		{
			ObjectTools::DeleteSingleObject(AnimNextModule);
			AnimNextModule = nullptr;
		}
	}
	
	// QMetry Test Case:  UE-TC-10714
	TEST_METHOD(Save_AnimNext_Module_Without_Workspace)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Workspaxce Asset"), [&]()
			{
				CreateAnimNextWorkspace();
			})
			.Then(TEXT("Create AnimNext Module Asset"), [&]()
			{
				CreateAnimNextModule();
			})
			.Then(TEXT("Assert that UAF Assets were created"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextWorkspace));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextWorkspace)));
				ASSERT_THAT(IsNotNull(AnimNextModule));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextModule)));
			})
			.Then(TEXT("Assert that UAF Assets were saved"), [&]()
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;				
				ASSERT_THAT(IsTrue(UPackage::SavePackage(AnimNextWorkspacePackage, nullptr, *(AnimNextWorkspacePackageName.ToString()), SaveArgs)));
				ASSERT_THAT(IsTrue(UPackage::SavePackage(AnimNextModulePackage, nullptr, *(AnimNextModulePackageName.ToString()), SaveArgs)));
			});
	}
		
	// QMetry Test Case:  UE-TC-18778
	TEST_METHOD(Undo_Redo_In_AnimNext_Module)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Module Asset"), [&]()
			{
				CreateAnimNextModule();
			})
			.Then(TEXT("Create a Write Pose node"), [&]()
			{
				EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(AnimNextModule);
				ASSERT_THAT(IsNotNull(EditorData));
				ControllerGraph = EditorData->GetControllerByName("RigVMGraph")->GetGraph();
				ASSERT_THAT(IsNotNull(ControllerGraph));				
				ParentGraph = Cast<UEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ControllerGraph));
				ASSERT_THAT(IsNotNull(ParentGraph));
				
				TArray<UEdGraphPin*> FromPins;
				WritePoseNode = UAFTestsUtilities::AddUnitNode(ParentGraph, "/Script/AnimNext.RigUnit_AnimNextWriteSkeletalMeshComponentPose", FromPins, FVector2f(250.0f, 135.0f));
				ASSERT_THAT(IsNotNull(WritePoseNode));
			})
			.Then(TEXT("Create a bool Variable"), [&]()
			{
				FScopedTransaction Transaction(NSLOCTEXT("Add_Variables", "AddVariables", "Add Variable(s)"));

				UAnimNextVariableEntry* VariableEntry = UAFTestsUtilities::AddVariable(AnimNextModule, FAnimNextParamType::GetType<bool>(), VariableName, "false");
				ASSERT_THAT(IsNotNull(VariableEntry));
			})
			.Then(TEXT("Add Get Variable Node to Graph"), [&]()
			{
				TArray<UEdGraphPin*> FromPins;
				UEdGraphNode* GetVariableNode = UAFTestsUtilities::AddVariableNode(ParentGraph, AnimNextModule, VariableName, FAnimNextParamType::GetType<bool>(), FAnimNextSchemaAction_Variable::EVariableAccessorChoice::Get, FromPins, FVector2f(-250.0f, 135.0f));
				ASSERT_THAT(IsNotNull(GetVariableNode));
			})
			.Then(TEXT("Add Set Variable Node to Graph"), [&]()
			{
				TArray<UEdGraphPin*> FromPins;
				UEdGraphNode* SetVariableNode = UAFTestsUtilities::AddVariableNode(ParentGraph, AnimNextModule, VariableName, FAnimNextParamType::GetType<bool>(), FAnimNextSchemaAction_Variable::EVariableAccessorChoice::Set, FromPins, FVector2f(-250.0f, 235.0f));
				ASSERT_THAT(IsNotNull(SetVariableNode));
			})
			.Until(TEXT("Execute Undo Set Variable Node"), [&]()
			{
				return GEditor->UndoTransaction();
			})
			.Until(TEXT("Execute Undo Get Variable Node"), [&]()
			{
				return GEditor->UndoTransaction();
			})
			.Until(TEXT("Execute Undo Create bool Variable"), [&]()
			{
				return GEditor->UndoTransaction();
			})		
			.Until(TEXT("Execute Undo Create Write Pose Node"), [&]()
			{
				return GEditor->UndoTransaction();
			})
			.Then(TEXT("Assert Undos"), [&]()
			{
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(SetVariableNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(GetVariableNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
				ASSERT_THAT(IsNull(EditorData->FindEntry(FName(VariableName))));
			})
			.Until(TEXT("Execute Redo Create Write Pose Node"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Until(TEXT("Execute Redo Create bool Variable"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Until(TEXT("Execute Redo Get Variable Node"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Until(TEXT("Execute Redo Set Variable Node"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Then(TEXT("Assert Redos"), [&]()
			{
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(SetVariableNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(GetVariableNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
			});
	}

	// QMetry Test Case:  UE-TC-18832
	TEST_METHOD(Collapse_Node_To_Function_In_AnimNext_Module)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Module Asset"), [&]()
			{
				CreateAnimNextModule();
			})
			.Then(TEXT("Create a Run Graph node"), [&]()
			{
				EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(AnimNextModule);
				ASSERT_THAT(IsNotNull(EditorData));
				ControllerGraph = EditorData->GetControllerByName("RigVMGraph")->GetGraph();
				ASSERT_THAT(IsNotNull(ControllerGraph));				
				ParentGraph = Cast<UEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ControllerGraph));
				ASSERT_THAT(IsNotNull(ParentGraph));	
				
				TArray<UEdGraphPin*> FromPins;
				RunGraphNode = UAFTestsUtilities::AddUnitNode(ParentGraph, "/Script/AnimNextAnimGraph.RigUnit_AnimNextRunAnimationGraph_v2", FromPins, FVector2f(64.0f, 16.0f));
				ASSERT_THAT(IsNotNull(RunGraphNode));
			})
			.Then(TEXT("Create a Write Pose node"), [&]()
			{
				TArray<UEdGraphPin*> FromPins;
				WritePoseNode = UAFTestsUtilities::AddUnitNode(ParentGraph, "/Script/AnimNext.RigUnit_AnimNextWriteSkeletalMeshComponentPose", FromPins, FVector2f(336.0f, 0.0f));
				ASSERT_THAT(IsNotNull(WritePoseNode));
			})
			.Then(TEXT("Add link from PrePhysics node Execute pin to Run Graph node Execute pin"), [&]()
			{
				ASSERT_THAT(IsTrue(UAFTestsUtilities::AddLink(AnimNextModule, "RigUnit_AnimNextPrePhysicsEvent.ExecuteContext", "AnimNextRunAnimationGraph_v2.ExecuteContext")));
			})
			.Then(TEXT("Add link from Run Graph node output pin to Write Pose node Execute pin"), [&]()
			{				
				ASSERT_THAT(IsTrue(UAFTestsUtilities::AddLink(AnimNextModule, "AnimNextRunAnimationGraph_v2.ExecuteContext", "AnimNextWriteSkeletalMeshComponentPose.ExecuteContext")));
			})
			.Then(TEXT("Collpase Run Graph and Write Pose nodes to a Function"), [&]()
			{
				TArray<FName> Nodes;
				Nodes.Add(FName(RunGraphNodeName));
				Nodes.Add(FName(WritePoseNodeName));
				URigVMCollapseNode* CollapseNode = UAFTestsUtilities::CollapseNodes(AnimNextModule, Nodes, FunctionNodeName, true);
				ASSERT_THAT(IsNotNull(CollapseNode));
			})		
			.Until(TEXT("Execute Undo Collpase Nodes"), [&]()
			{
				return GEditor->UndoTransaction();
			})
			.Then(TEXT("Assert Undo Collapse Nodes"), [&]()
			{
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(RunGraphNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(FunctionNodeName))));
			})
			.Until(TEXT("Execute Redo Collapse Nodes"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Then(TEXT("Assert Redo Collapse Nodes"), [&]()
			{
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(RunGraphNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(FunctionNodeName))));
			});		
	}

	// QMetry Test Case:  UE-TC-18833
	TEST_METHOD(Collapse_Node_To_Node_In_AnimNext_Module)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Module Asset"), [&]()
			{
				CreateAnimNextModule();
			})
			.Then(TEXT("Create a Run Graph node"), [&]()
			{
				EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(AnimNextModule);
				ASSERT_THAT(IsNotNull(EditorData));
				ControllerGraph = EditorData->GetControllerByName("RigVMGraph")->GetGraph();
				ASSERT_THAT(IsNotNull(ControllerGraph));				
				ParentGraph = Cast<UEdGraph>(EditorData->GetEditorObjectForRigVMGraph(ControllerGraph));
				ASSERT_THAT(IsNotNull(ParentGraph));	
				
				TArray<UEdGraphPin*> FromPins;
				RunGraphNode = UAFTestsUtilities::AddUnitNode(ParentGraph, "/Script/AnimNextAnimGraph.RigUnit_AnimNextRunAnimationGraph_v2", FromPins, FVector2f(64.0f, 16.0f));
				ASSERT_THAT(IsNotNull(RunGraphNode));
			})
			.Then(TEXT("Create a Write Pose node"), [&]()
			{
				TArray<UEdGraphPin*> FromPins;
				WritePoseNode = UAFTestsUtilities::AddUnitNode(ParentGraph, "/Script/AnimNext.RigUnit_AnimNextWriteSkeletalMeshComponentPose", FromPins, FVector2f(336.0f, 0.0f));
				ASSERT_THAT(IsNotNull(WritePoseNode));
			})
			.Then(TEXT("Add link from PrePhysics node Execute pin to Run Graph node Execute pin"), [&]()
			{
				ASSERT_THAT(IsTrue(UAFTestsUtilities::AddLink(AnimNextModule, "RigUnit_AnimNextPrePhysicsEvent.ExecuteContext", "AnimNextRunAnimationGraph_v2.ExecuteContext")));
			})
			.Then(TEXT("Add link from Run Graph node output pin to Write Pose node Execute pin"), [&]()
			{				
				ASSERT_THAT(IsTrue(UAFTestsUtilities::AddLink(AnimNextModule, "AnimNextRunAnimationGraph_v2.ExecuteContext", "AnimNextWriteSkeletalMeshComponentPose.ExecuteContext")));
			})
			.Then(TEXT("Collpase Run Graph and Write Pose nodes"), [&]()
			{
				TArray<FName> Nodes;
				Nodes.Add(FName(RunGraphNodeName));
				Nodes.Add(FName(WritePoseNodeName));
				URigVMCollapseNode* CollapseNode = UAFTestsUtilities::CollapseNodes(AnimNextModule, Nodes);
				ASSERT_THAT(IsNotNull(CollapseNode));
			})
			.Until(TEXT("Execute Undo Collpase Nodes"), [&]()
			{
				return GEditor->UndoTransaction();
			})
			.Then(TEXT("Assert Undo Collapse Nodes"), [&]()
			{
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(RunGraphNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(CollapseNodeName))));
			})
			.Until(TEXT("Execute Redo Collapse Nodes"), [&]()
			{
				return GEditor->RedoTransaction();
			})
			.Then(TEXT("Assert Redo Collapse Nodes"), [&]()
			{
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(RunGraphNodeName))));
				ASSERT_THAT(IsNull(ControllerGraph->FindNodeByName(FName(WritePoseNodeName))));
				ASSERT_THAT(IsNotNull(ControllerGraph->FindNodeByName(FName(CollapseNodeName))));
			});
	}
	
protected:
	void CreateAnimNextModule()
	{
		UFactory* Factory = NewObject<UAnimNextModuleFactory>();
		AnimNextModulePackage = CreateUniquePackage("NewAnimNextModule");
		AnimNextModulePackageName = *FPaths::GetBaseFilename(AnimNextModulePackage->GetName());		
		UObject* FactoryObject = Factory->FactoryCreateNew(UAnimNextModule::StaticClass(), AnimNextModulePackage, AnimNextModulePackageName, RF_Public | RF_Standalone, NULL, GWarn);
		FAssetRegistryModule::AssetCreated(FactoryObject);
		AnimNextModule = CastChecked<UAnimNextModule>(FactoryObject);
	}
	
	void CreateAnimNextWorkspace()
	{
		UFactory* Factory = NewObject<UAnimNextWorkspaceFactory>();
		AnimNextWorkspacePackage = CreateUniquePackage("NewAnimNextWorkspace");
		AnimNextWorkspacePackageName = *FPaths::GetBaseFilename(AnimNextWorkspacePackage->GetName());		
		UObject* FactoryObject = Factory->FactoryCreateNew(UWorkspace::StaticClass(), AnimNextWorkspacePackage, AnimNextWorkspacePackageName, RF_Public | RF_Standalone, NULL, GWarn);
		FAssetRegistryModule::AssetCreated(FactoryObject);
		AnimNextWorkspace = CastChecked<UWorkspace>(FactoryObject);
	}
	
	UPackage* CreateUniquePackage(FString InAssetName)
	{
		// Create a unique package name and path
		const FString BasePackageName = "/Game/";    // aka “/All/Content/‘
		FString UniquePackageName;
		FString AssetName;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.CreateUniqueAssetName(BasePackageName, InAssetName, UniquePackageName, AssetName);
		return CreatePackage(*UniquePackageName);
	}
};

#endif // WITH_EDITOR
#endif // WITH_DEV_AUTOMATION_TESTS
