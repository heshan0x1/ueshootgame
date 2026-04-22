// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetWizard.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EditorModeManager.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IWorkspaceEditor.h"
#include "IWorkspaceEditorModule.h"
#include "TemplateDataAsset.h"
#include "Component/AnimNextComponent.h"
#include "Factories/BlueprintFactory.h"
#include "Module/AnimNextModule.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Wizard/STemplatePicker.h"
#include "WorkspaceEditor/Private/Workspace.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Workspace/AnimNextWorkspaceEditorMode.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "IWorkspacePicker.h"
#include "ObjectTools.h"
#include "TemplateConfig.h"

#define LOCTEXT_NAMESPACE "UE::UAF::Editor::Wizard"

namespace UE::UAF::Editor
{
	TObjectPtr<UWorkspace> FAssetWizard::ShowWorkspacePicker()
	{
		const Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");

		Workspace::IWorkspacePicker::FConfig WorkspacePickerConfig;
		WorkspacePickerConfig.HintText = Workspace::IWorkspacePicker::EHintText::CreateOrUseExistingWorkspace;
		WorkspacePickerConfig.WorkspaceFactoryClass = UAnimNextWorkspaceFactory::StaticClass();
		
		const TSharedPtr<Workspace::IWorkspacePicker> WorkspacePicker = WorkspaceEditorModule.CreateWorkspacePicker(WorkspacePickerConfig);
		WorkspacePicker->ShowModal();

		return Cast<UWorkspace>(WorkspacePicker->GetSelectedWorkspace());
	}

	void FAssetWizard::HandleTemplateSelected(const TObjectPtr<const UUAFTemplateDataAsset> Template, const TObjectPtr<const UUAFTemplateConfig> TemplateConfig, const TObjectPtr<UWorkspace> Workspace)
	{
		struct FAssetPair
		{
			TObjectPtr<UObject> TemplateAsset;
			TObjectPtr<UObject> UserAsset;
		};
		TArray<FAssetPair> Assets;

		bool bFailedToInstantiateTemplateAssets = false;

		for (int32 TemplateAssetIndex = 0; TemplateAssetIndex < Template->Assets.Num(); ++TemplateAssetIndex)
		{
			TObjectPtr<UObject> TemplateAsset  = Template->Assets[TemplateAssetIndex];
			
			FAssetData AssetData(TemplateAsset);

			const FString AssetName = TemplateConfig->AssetNaming[TemplateAssetIndex];
			TObjectPtr<UObject> NewAsset = IAssetTools::Get().DuplicateAsset(AssetName, TemplateConfig->OutputPath.Path, TemplateAsset);
			
			if (NewAsset != nullptr)
			{
				FAssetPair& AssetPair = Assets.AddDefaulted_GetRef();
				AssetPair.TemplateAsset = TemplateAsset;
				AssetPair.UserAsset = NewAsset;
			}
			else
			{
				// Failed to instantiate template
				bFailedToInstantiateTemplateAssets = true;
				break;
			}
		}

		if (bFailedToInstantiateTemplateAssets)
		{
			for (FAssetPair& Asset : Assets)
			{
				ObjectTools::DeleteSingleObject(Asset.UserAsset);
			}

			UE_LOG(LogAnimation, Warning, TEXT("Failed to instantiate template assets"));
			return;
		}
		
		for (int32 AssetIndex = 0; AssetIndex < Assets.Num(); ++AssetIndex)
		{
			// This asset will have all of its references to any of the template assets updates to point to the new user asset.
			// Any references inside this asset to other assets not part of the template asset pack will remain the same.
			TObjectPtr<UObject> AssetToRepair = Assets[AssetIndex].UserAsset;

			TMap<UObject*, UObject*> ReplaceMap;
			
			for (int32 OtherAssetIndex = 0; OtherAssetIndex < Assets.Num(); ++OtherAssetIndex)
			{
				if (AssetIndex != OtherAssetIndex)
				{
					FAssetPair OtherAssetPair = Assets[OtherAssetIndex];
					ReplaceMap.Add(OtherAssetPair.TemplateAsset, OtherAssetPair.UserAsset);
				}
			}
			
			FArchiveReplaceObjectRef<UObject> ReplaceAr(AssetToRepair, ReplaceMap);
			AssetToRepair->PostEditChange();
		}

		for (const FAssetPair& AssetPair : Assets)
		{
			Workspace->AddAsset(AssetPair.UserAsset);
		}
		
		if (Template->AssetToOpen)
		{
			const int32 AssetIndex = Assets.IndexOfByPredicate([Template](const FAssetPair& InAssetPair){ return InAssetPair.TemplateAsset == Template->AssetToOpen; });

			if (Assets.IsValidIndex(AssetIndex))
			{
				TObjectPtr<UObject> AssetToOpen = Assets[AssetIndex].UserAsset;
				
				Workspace::IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<Workspace::IWorkspaceEditorModule>("WorkspaceEditor");
				Workspace::IWorkspaceEditor* WorkspaceEditor = WorkspaceEditorModule.OpenWorkspaceForObject(AssetToOpen, Workspace::EOpenWorkspaceMethod::Default, UAnimNextWorkspaceFactory::StaticClass());

				UAnimNextWorkspaceEditorMode* EditorMode = Cast<UAnimNextWorkspaceEditorMode>(WorkspaceEditor->GetEditorModeManager().GetActiveScriptableMode(UAnimNextWorkspaceEditorMode::EM_AnimNextWorkspace));
				EditorMode->HandleCompile();
			}
		}

		if (TemplateConfig->BlueprintMode == ETemplateBlueprintMode::CreateNewBlueprint || TemplateConfig->BlueprintMode == ETemplateBlueprintMode::ModifyExistingBlueprint)
		{
			TObjectPtr<UBlueprint> Blueprint = [TemplateConfig]()
			{
				if (TemplateConfig->BlueprintMode == ETemplateBlueprintMode::CreateNewBlueprint)
				{
					TObjectPtr<UBlueprintFactory> BlueprintFactory = NewObject<UBlueprintFactory>();
					BlueprintFactory->AddToRoot();
			
					BlueprintFactory->ParentClass = TemplateConfig->BlueprintClass.Get()
						? TemplateConfig->BlueprintClass.Get()
						: AActor::StaticClass();

					FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
					IAssetTools& AssetTools = AssetToolsModule.Get();

					TObjectPtr<UObject> NewAsset = AssetTools.CreateAsset(TemplateConfig->BlueprintAssetName, TemplateConfig->OutputPath.Path, UBlueprint::StaticClass(), BlueprintFactory);

					return CastChecked<UBlueprint>(NewAsset);
				}
				else
				{
					return TemplateConfig->BlueprintToModify.LoadSynchronous();
				}
			}();
			
			TObjectPtr<AActor> Actor = CastChecked<AActor>(Blueprint->GeneratedClass->GetDefaultObject());

			USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(Actor->GetComponentByClass(USkeletalMeshComponent::StaticClass()));
			UAnimNextComponent* UAFComp = Cast<UAnimNextComponent>(Actor->GetComponentByClass(UAnimNextComponent::StaticClass()));

			const FAssetPair* ModuleAssetPair = Assets.FindByPredicate([](const FAssetPair& InAssetPair)
			{
				return InAssetPair.UserAsset->GetClass() == UAnimNextModule::StaticClass();
			});

			// Create BP components
			{
				USimpleConstructionScript* ConstructionScript = Blueprint->SimpleConstructionScript;
				USCS_Node* DefaultRootNode = ConstructionScript->GetDefaultSceneRootNode();
				
				if (!SkelMeshComp)
				{
					// No SkelMesh component exists in this blueprint so create one
					USCS_Node* SkelMeshCompNode = ConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), "Mesh");
					if (DefaultRootNode)
					{
						DefaultRootNode->AddChildNode(SkelMeshCompNode);
					}
					else
					{
						ConstructionScript->AddNode(SkelMeshCompNode);
					}

					SkelMeshComp = CastChecked<USkeletalMeshComponent>(SkelMeshCompNode->ComponentTemplate);
				}

				if (!UAFComp)
				{
					// No AnimNext component exists in this blueprint so create one
					USCS_Node* UAFCompNode = ConstructionScript->CreateNode(UAnimNextComponent::StaticClass(), "AnimationFramework");
					ConstructionScript->AddNode(UAFCompNode);

					UAFComp = CastChecked<UAnimNextComponent>(UAFCompNode->ComponentTemplate);
				}
			}

			// Configure BP components
			{
				if (SkelMeshComp)
				{
					SkelMeshComp->SetSkeletalMesh(TemplateConfig->SkeletalMesh.LoadSynchronous());
					SkelMeshComp->SetEnableAnimation(false);
					
					FTransform RelativeTransform;
					RelativeTransform.SetTranslation(FVector(0, 0, -90.0));
					RelativeTransform.SetRotation(FQuat::MakeFromEuler(FVector(0, 0, -90.0)));
					SkelMeshComp->SetRelativeTransform(RelativeTransform);
				}

				if (UAFComp && ModuleAssetPair && SkelMeshComp && SkelMeshComp->GetSkeletalMeshAsset())
				{
					UAFComp->SetModule(CastChecked<UAnimNextModule>(ModuleAssetPair->UserAsset.Get()));
				}
			}

			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}
	}

	void FAssetWizard::ShowTemplatePicker()
	{
		const TObjectPtr<UWorkspace> Workspace = ShowWorkspacePicker();
		if (!Workspace)
		{
			return;
		}
		
		TSharedRef<SWindow> TemplateWindow = SNew(SWindow)
			.Title(LOCTEXT("AssetWizard", "UAF Asset Wizard"))
			.ClientSize(FVector2D(1200, 800))
			.SupportsMinimize(false)
			.SupportsMaximize(false);

		TemplateWindow->SetContent(
			SNew(STemplatePicker)
			.OnTemplateSelected_Lambda([TemplateWindow, Workspace](const TObjectPtr<const UUAFTemplateDataAsset> InTemplate, const TObjectPtr<const UUAFTemplateConfig> InTemplateConfig) mutable
			{
				HandleTemplateSelected(InTemplate, InTemplateConfig, Workspace);
				FSlateApplication::Get().RequestDestroyWindow(TemplateWindow);
			})
			.OnCancel_Lambda([TemplateWindow]()
			{
				FSlateApplication::Get().RequestDestroyWindow(TemplateWindow);
			})
		);

		GetMutableDefault<UUAFTemplateConfig>()->OutputPath.Path = FAssetData(Workspace).PackagePath.ToString();
		
		FSlateApplication::Get().AddWindow(TemplateWindow);
	}
	
	void FAssetWizard::Launch()
	{
		ShowTemplatePicker();		
	}
}

#undef LOCTEXT_NAMESPACE