// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#if WITH_EDITOR

#include "UAFTestsUtilities.h"
#include "CQTest.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WorkspaceEditor/Private/Workspace.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Graph/AnimNextAnimationGraphFactory.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModuleFactory.h"

TEST_CLASS(FUAFWorkspaceTests, "Animation.UAF.Functional")
{
	UWorkspace* WorkspaceAsset = nullptr;
	UWorkspace* WorkspaceSecondaryAsset = nullptr;
	UAnimNextAnimationGraph* AnimationGraphAsset = nullptr;
	UAnimNextModule* ModuleAsset = nullptr;
	UAssetEditorSubsystem* AssetEditorSubsystem = nullptr;

	BEFORE_EACH()
	{
		AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		ASSERT_THAT(IsNotNull(AssetEditorSubsystem));
	}

	// QMetry Test Case:  UE-TC-10700
	TEST_METHOD(Verify_Add_Animation_Graph_To_Workspace)
	{
		TestCommandBuilder
			.Do(TEXT("Create Animation Graph Asset"), [&]()
			{
				const TSubclassOf<UAnimNextAnimationGraphFactory> AnimGraphFactoryClass = UAnimNextAnimationGraphFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextAnimationGraphFactory>(), UAnimNextAnimationGraph::StaticClass(), "NewAnimNextAnimationGraph");
				AnimationGraphAsset = CastChecked<UAnimNextAnimationGraph>(FactoryObject);
				ASSERT_THAT(IsNotNull(AnimationGraphAsset));
			})
			.Then(TEXT("Create UAF Workspace Asset"), [&]()
			{
				const TSubclassOf<UAnimNextWorkspaceFactory> WorkSpaceFactoryClass = UAnimNextWorkspaceFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextWorkspaceFactory>(), UWorkspace::StaticClass(), "NewAnimNextWorkspace");
				WorkspaceAsset = CastChecked<UWorkspace>(FactoryObject);
				ASSERT_THAT(IsNotNull(WorkspaceAsset));
			})
			.Then(TEXT("Add Animation Graph to Workspace and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsTrue(WorkspaceAsset->AddAsset(AnimationGraphAsset)));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(WorkspaceAsset)));
			});
	}
	
	// QMetry Test Case:  UE-TC-10716
	TEST_METHOD(Verify_Add_Module_To_Multiple_Workspaces)
	{
		TestCommandBuilder
			.Do(TEXT("Create UAF Module Asset"), [&]()
			{
				const TSubclassOf<UAnimNextModuleFactory> ModuleFactoryClass = UAnimNextModuleFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextModuleFactory>(), UAnimNextModule::StaticClass(), "NewAnimNextModule");				
				ModuleAsset = CastChecked<UAnimNextModule>(FactoryObject);
				ASSERT_THAT(IsNotNull(ModuleAsset));
			})
			.Then(TEXT("Create UAF Workspace Asset"), [&]()
			{
				const TSubclassOf<UAnimNextWorkspaceFactory> WorkSpaceFactoryClass = UAnimNextWorkspaceFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextWorkspaceFactory>(), UWorkspace::StaticClass(), "NewAnimNextWorkspace");
				WorkspaceAsset = CastChecked<UWorkspace>(FactoryObject);
				ASSERT_THAT(IsNotNull(WorkspaceAsset));
			})
			.Then(TEXT("Create Another UAF Workspace Asset"), [&]()
			{
				const TSubclassOf<UAnimNextWorkspaceFactory> WorkSpaceFactoryClass = UAnimNextWorkspaceFactory::StaticClass();
				UObject* FactoryObject = UAFTestsUtilities::CreateFactoryObject(NewObject<UAnimNextWorkspaceFactory>(), UWorkspace::StaticClass(), "NewAnimNextWorkspace1");
				WorkspaceSecondaryAsset = CastChecked<UWorkspace>(FactoryObject);
				ASSERT_THAT(IsNotNull(WorkspaceSecondaryAsset));
			})		
			.Then(TEXT("Add Module to Multiple Workspaces and Open in Editor"), [&]()
			{
				ASSERT_THAT(IsTrue(WorkspaceAsset->AddAsset(ModuleAsset)));
				ASSERT_THAT(IsTrue(WorkspaceSecondaryAsset->AddAsset(ModuleAsset)));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(WorkspaceAsset)));
				ASSERT_THAT(IsTrue(AssetEditorSubsystem->OpenEditorForAsset(WorkspaceSecondaryAsset)));
			});
	}
};

#endif // WITH_EDITOR
#endif // WITH_DEV_AUTOMATION_TESTS
