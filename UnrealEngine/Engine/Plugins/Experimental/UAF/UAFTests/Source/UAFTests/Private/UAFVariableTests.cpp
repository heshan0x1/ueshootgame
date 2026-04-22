// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR

#include "UAFTestsUtilities.h"
#include "CQTest.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraphFactory.h"
#include "AnimNextRigVMAssetEditorData.h"

TEST_CLASS(FAssetVariableTests, "Animation.UAF.Functional")
{
	UAnimNextRigVMAsset* AnimNextAsset;
	UAssetEditorSubsystem* AssetEditorSubsystem;
	UAnimNextRigVMAssetEditorData* EditorData;

	BEFORE_EACH()
	{
		AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		ASSERT_THAT(IsNotNull(AssetEditorSubsystem));
	}

	// QMetry Test Case:  UE-TC-10712
	TEST_METHOD(AnimNextAnimationGraph_Create_Variable)
	{		
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Animation Graph Asset"), [&]()
			{
				const TSubclassOf<UAnimNextAnimationGraphFactory> AnimNextAnimGraphFactoryClass = UAnimNextAnimationGraphFactory::StaticClass();
				UObject* factoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextAnimationGraphFactory>(GetTransientPackage(), AnimNextAnimGraphFactoryClass.Get()), UAnimNextAnimationGraph::StaticClass(), "NewAnimNextAnimationGraph");
				AnimNextAsset = CastChecked<UAnimNextAnimationGraph>(factoryObject);				
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));
			})
			.Then(TEXT("Create Bool Variable"), [&]()
			{
				FAnimNextParamType Type = FAnimNextParamType::GetType<bool>();
				UAnimNextVariableEntry* VariableEntry = UAFTestsUtilities::AddVariable(AnimNextAsset, Type, "NewVariable", "false");
				ASSERT_THAT(IsNotNull(VariableEntry));
			});
	}	
};

#endif // WITH_EDITOR
#endif // WITH_DEV_AUTOMATION_TESTS
