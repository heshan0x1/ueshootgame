// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR

#include "UAFTestsUtilities.h"
#include "CQTest.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WorkspaceEditor/Private/Workspace.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "AnimNextStateTree.h"
#include "AnimNextStateTreeFactory.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraphFactory.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModuleFactory.h"
#include "Variables/AnimNextSharedVariables.h"
#include "Variables/AnimNextSharedVariablesFactory.h"

// QMetry Test Case:  UE-TC-10715 
TEST_CLASS(FAssetCreationTests, "Animation.UAF.Functional")
{
	UObject* AnimNextAsset;
	UAssetEditorSubsystem* AssetEditorSubsystem;

	BEFORE_EACH()
	{
		AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		ASSERT_THAT(IsNotNull(AssetEditorSubsystem));
	}

	TEST_METHOD(Verify_Workspace_Asset_Creation)
	{		
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Workspace Asset"), [&]()
			{
				const TSubclassOf<UAnimNextWorkspaceFactory> WorkSpaceFactoryClass = UAnimNextWorkspaceFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextWorkspaceFactory>(), UWorkspace::StaticClass(), "NewAnimNextWorkspace");
				AnimNextAsset = CastChecked<UWorkspace>(FactoryObject);
			})
			.Then(TEXT("Assert AnimNext Workspace Asset Created and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextAsset));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));
			});
	}
	
	TEST_METHOD(Verify_StateTree_Asset_Creation)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext State Tree Asset"), [&]()
			{
				const TSubclassOf<UAnimNextStateTreeFactory> StateTreeFactoryClass = UAnimNextStateTreeFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextStateTreeFactory>(), UAnimNextStateTree::StaticClass(), "NewAnimNextStateTree");
				AnimNextAsset = CastChecked<UAnimNextStateTree>(FactoryObject);
			})
			.Then(TEXT("Assert AnimNext State Tree Asset Created and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextAsset));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));
			});
	}
	
	TEST_METHOD(Verify_AnimGraph_Asset_Creation)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Animation Graph Asset"), [&]()
			{
				const TSubclassOf<UAnimNextAnimationGraphFactory> AnimGraphFactoryClass = UAnimNextAnimationGraphFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextAnimationGraphFactory>(), UAnimNextAnimationGraph::StaticClass(), "NewAnimNextAnimationGraph");
				AnimNextAsset = CastChecked<UAnimNextAnimationGraph>(FactoryObject);
			})
			.Then(TEXT("Assert AnimNext Animation Graph Asset Created and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextAsset));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));
			});		
	}
	
	TEST_METHOD(Verify_Module_Asset_Creation)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Module Asset"), [&]()
			{
				const TSubclassOf<UAnimNextModuleFactory> ModuleFactoryClass = UAnimNextModuleFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextModuleFactory>(), UAnimNextModule::StaticClass(), "NewAnimNextModule");				
				AnimNextAsset = CastChecked<UAnimNextModule>(FactoryObject);
			})
			.Then(TEXT("Assert AnimNext Module Asset Created and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextAsset));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));
			});
	}
	
	TEST_METHOD(Verify_SharedVariables_Asset_Creation)
	{
		TestCommandBuilder
			.Do(TEXT("Create AnimNext Shared Variables Asset"), [&]()
			{
				const TSubclassOf<UAnimNextSharedVariablesFactory> SharedVariablesFactoryClass = UAnimNextSharedVariablesFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextSharedVariablesFactory>(), UAnimNextSharedVariables::StaticClass(), "NewAnimNextSharedVariables");
				AnimNextAsset = CastChecked<UAnimNextSharedVariables>(FactoryObject);
			})
			.Then(TEXT("Assert AnimNext Shared Variables Asset Created and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsNotNull(AnimNextAsset));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(AnimNextAsset)));		
			});
	}	
};

#endif // WITH_EDITOR
#endif // WITH_DEV_AUTOMATION_TESTS
