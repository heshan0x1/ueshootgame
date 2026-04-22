// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR

#include "UAFTestsUtilities.h"

namespace UAFTestsUtilities
{
	UObject* CreateFactoryObject(UFactory* InFactory, UClass* InClass, const FString& InPackageName)
	{
		if (!InClass || !InFactory)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid pointer instance provided."));
			return nullptr;
		}
		
		UPackage* NewPackage = CreatePackage(nullptr);
		if (!NewPackage)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to create UPackage instance."));
			return nullptr;			
		}
		
		FName NewPackageName = InPackageName.Equals("") ? *FPaths::GetBaseFilename(NewPackage->GetName()) : FName(InPackageName);
		
		return InFactory->FactoryCreateNew(InClass, NewPackage, NewPackageName, RF_Public | RF_Standalone, NULL, GWarn);
	}

	UEdGraphNode* AddUnitNode(UEdGraph* InParentGraph, const FString& InScriptStructPath, TArray<UEdGraphPin*>& InFromPins, const FVector2f& InRigUnitLocation)
	{		
		if (!InParentGraph)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid UEdGraph instance provided."));
			return nullptr;
		}
		
		UScriptStruct* ScriptStruct = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UScriptStruct>(InScriptStructPath);
		FText Category = NSLOCTEXT("Category", "Category", "Category");
		FText MenuDesc = NSLOCTEXT("Menu_Desc", "MenuDesc", "Menu Desc");
		FText ToolTip = NSLOCTEXT("Tool_Tip", "ToolTip", "Tool Tip");
		TSharedPtr<FAnimNextSchemaAction_RigUnit> RigUnitAction = MakeShared<FAnimNextSchemaAction_RigUnit>(URigVMUnitNode::StaticClass(), ScriptStruct, Category, MenuDesc, ToolTip);

		return RigUnitAction->PerformAction(InParentGraph, InFromPins, InRigUnitLocation);
	}

	URigVMLibraryNode* AddFunctionNode(UAnimNextRigVMAsset* InAnimNextAsset, const FString& InFunctionName)
	{
		if (!InAnimNextAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid UAnimNextRigVMAsset instance provided."));
			return nullptr;
		}
		
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
		return EditorData->AddFunction(FName(InFunctionName), true);
	}

	UAnimNextVariableEntry* AddVariable(UAnimNextRigVMAsset* InAnimNextAsset, const FAnimNextParamType& InVariableType, const FString& InVariableName, const FString& InDefaultValue)
	{
		if (!InAnimNextAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid UAnimNextRigVMAsset instance provided."));
			return nullptr;
		}
		
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
		return EditorData->AddVariable(FName(InVariableName), InVariableType, InDefaultValue);
	}

		UEdGraphNode* AddVariableNode(UEdGraph* InParentGraph, const UObject* InSourceObject, const FString& InVariableName, const FAnimNextParamType& InVariableType, const FAnimNextSchemaAction_Variable::EVariableAccessorChoice InVariableAccessorChoice, TArray<UEdGraphPin*>& InFromPins, const FVector2f& InVariableLocation)
	{
		if (!InParentGraph)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid UEdGraph instance provided."));
			return nullptr;
		}
				
		TSharedPtr<FAnimNextSchemaAction_Variable> VariableAction = MakeShared<FAnimNextSchemaAction_Variable>(FName(InVariableName), InSourceObject, InVariableType, InVariableAccessorChoice);
		return VariableAction->PerformAction(InParentGraph, InFromPins, InVariableLocation);
	}

	URigVMPin* AddPin(UAnimNextRigVMAsset* InAnimNextAsset, URigVMLibraryNode* InLibraryNode, ERigVMPinDirection InDirection, const FString& InCPPName, const FString& InCPPType, const FName& InCPPTypeObjectPath, const FString& InDefaultValue)
	{
		if (!InAnimNextAsset || !InLibraryNode)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid pointer instance provided."));
			return nullptr;
		}

		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
		URigVMGraph* FunctionGraph = InLibraryNode->GetContainedGraph();
		URigVMController* Controller = CastChecked<IRigVMClientHost>(EditorData)->GetController(FunctionGraph);		
		const FName NewPinName = Controller->AddExposedPin(FName(InCPPName), InDirection, InCPPType, InCPPTypeObjectPath, InDefaultValue);
		
		URigVMPin* NewPin;
		switch (InDirection)
		{
			case ERigVMPinDirection::Input:
				NewPin = InLibraryNode->GetEntryNode()->FindPin(NewPinName.ToString());			
				break;
			case ERigVMPinDirection::Output:
				NewPin = InLibraryNode->GetReturnNode()->FindPin(NewPinName.ToString());
				break;
			default:
				NewPin = nullptr;
				break;
		}
		
		return NewPin;
	}

	bool AddLink(UAnimNextRigVMAsset* InAnimNextAsset, const FString& InOutputPinPath, const FString& InInputPinPath)
	{
		if (!InAnimNextAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid pointer instance provided."));
			return false;
		}
		
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
		URigVMController* Controller = CastChecked<IRigVMClientHost>(EditorData)->GetControllerByName("RigVMGraph");
		return Controller->AddLink(InOutputPinPath, InInputPinPath);
	}

	bool SetNodeSelection(UAnimNextRigVMAsset* InAnimNextAsset, const TArray<FName>& InNodeNames)
	{
		if (!InAnimNextAsset)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid pointer instance provided."));
			return false;
		}
		
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
		URigVMController* Controller = CastChecked<IRigVMClientHost>(EditorData)->GetControllerByName("RigVMGraph");
		return Controller->SetNodeSelection(InNodeNames);
	}

	URigVMCollapseNode* CollapseNodes(UAnimNextRigVMAsset* InAnimNextAsset, const TArray<FName>& InNodeNames, const FString& InCollapseNodeName, bool InCollapseToFunction)
	{
		if (SetNodeSelection(InAnimNextAsset, InNodeNames))
		{
			UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData(InAnimNextAsset);
			URigVMController* Controller = CastChecked<IRigVMClientHost>(EditorData)->GetControllerByName("RigVMGraph");			
			Controller->OpenUndoBracket(TEXT("Collapse Node"));
			URigVMCollapseNode* CollapseNode = Controller->CollapseNodes(InNodeNames, InCollapseNodeName);
			if (InCollapseToFunction)
			{
				Controller->PromoteCollapseNodeToFunctionReferenceNode(CollapseNode->GetFName());
			}
			Controller->CloseUndoBracket();
			return CollapseNode;
		}
		
		return nullptr;
	}
}

#endif
#endif
