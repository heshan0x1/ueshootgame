// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeEditorModule.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "HoldoutCompositeComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IConcertClientWorkspace.h"
#include "IConcertSyncClient.h"
#include "IConcertSyncClientModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "CompositeActor.h"
#include "CompositeEditorCommands.h"
#include "CompositeEditorStyle.h"
#include "PropertyEditorModule.h"
#include "Customizations/CompositeActorCustomization.h"
#include "Customizations/CompositeLayerPlateCustomization.h"
#include "Customizations/CompositeLayerSceneCaptureCustomization.h"
#include "Customizations/CompositeLayerShadowReflectionCustomization.h"
#include "Customizations/CompositeLayerSingleLightShadowCustomization.h"
#include "Layers/CompositeLayerPlate.h"
#include "Layers/CompositeLayerSceneCapture.h"
#include "Layers/CompositeLayerShadowReflection.h"
#include "Layers/CompositeLayerSingleLightShadow.h"
#include "Misc/MessageDialog.h"
#include "UI/SCompositeEditorPanel.h"

#define LOCTEXT_NAMESPACE "CompositeEditorModule"

DEFINE_LOG_CATEGORY(LogCompositeEditor);

void FCompositeEditorModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FCompositeEditorModule::OnPostEngineInit);

	UHoldoutCompositeComponent::OnComponentCreatedDelegate.AddRaw(this, &FCompositeEditorModule::TriggerHoldoutCompositeWarning);

	FCompositeEditorCommands::Register();
	FCompositeEditorStyle::Register();
	
	RegisterTabSpawners();
	RegisterCustomizations();
}

void FCompositeEditorModule::ShutdownModule()
{
	if (ConcertSyncClient.IsValid())
	{
		ConcertSyncClient->OnWorkspaceStartup().RemoveAll(this);
		ConcertSyncClient->OnWorkspaceShutdown().RemoveAll(this);
		
		TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin();
		if (WorkspacePtr.IsValid())
		{
			WorkspacePtr->OnFinalizeWorkspaceSyncCompleted().RemoveAll(this);
		}
	}

	UnregisterCustomizations();
	UnregisterTabSpawners();

	FCompositeEditorCommands::Unregister();
	FCompositeEditorStyle::Unregister();
	
	if (GEditor)
	{
		GEditor->OnLevelActorAdded().RemoveAll(this);
	}

	UHoldoutCompositeComponent::OnComponentCreatedDelegate.RemoveAll(this);
	
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
}

void FCompositeEditorModule::OnPostEngineInit()
{
	if (GEditor)
	{
		GEditor->OnLevelActorAdded().AddRaw(this, &FCompositeEditorModule::OnLevelActorAdded);
	}

	ConcertSyncClient = IConcertSyncClientModule::Get().GetClient(TEXT("MultiUser"));
	if (ConcertSyncClient.IsValid())
	{
		ConcertSyncClient->OnWorkspaceStartup().AddRaw(this, &FCompositeEditorModule::HandleWorkspaceStartup);
		ConcertSyncClient->OnWorkspaceShutdown().AddRaw(this, &FCompositeEditorModule::HandleWorkspaceShutdown);
	}
}

void FCompositeEditorModule::OnLevelActorAdded(AActor* InActor)
{
	if (!InActor || InActor->HasAnyFlags(RF_Transient) || !InActor->IsA<ACompositeActor>())
	{
		return;
	}

	UWorld* World = InActor->GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	int32 CompositeActorCount = 0;
	for (TActorIterator<ACompositeActor> It(World); It; ++It)
	{
		CompositeActorCount++;

		if (CompositeActorCount > 1)
		{
			ACompositeActor* CompositeActor = Cast<ACompositeActor>(InActor);
			if (IsValid(CompositeActor))
			{
				CompositeActor->SetActive(false);

				UE_LOG(LogCompositeEditor, Display, TEXT("No more than one composite actor should be active at the same time."));
			}
			break;
		}
	}
}

void FCompositeEditorModule::HandleWorkspaceStartup(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace)
{
	Workspace = NewWorkspace;

	if (NewWorkspace.IsValid())
	{
		NewWorkspace->OnFinalizeWorkspaceSyncCompleted().AddRaw(this, &FCompositeEditorModule::HandleFinalizeWorkspaceSyncCompleted);
	}
}

void FCompositeEditorModule::HandleWorkspaceShutdown(const TSharedPtr<IConcertClientWorkspace>& WorkspaceShuttingDown)
{
	if (WorkspaceShuttingDown == Workspace)
	{
		TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin();
		if (WorkspacePtr.IsValid())
		{
			WorkspacePtr->OnFinalizeWorkspaceSyncCompleted().RemoveAll(this);
		}

		Workspace.Reset();
	}
}

void FCompositeEditorModule::HandleFinalizeWorkspaceSyncCompleted()
{
	UWorld* World = nullptr;
	if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	if (IsValid(World))
	{
		for (TActorIterator<ACompositeActor> It(World); It; ++It)
		{
			ACompositeActor* CompositeActor = *It;
			if (IsValid(CompositeActor))
			{
				CompositeActor->PostJoinConcertSession();
			}
		}
	}
}

void FCompositeEditorModule::TriggerHoldoutCompositeWarning(const UHoldoutCompositeComponent* InComponent)
{
	static bool bWarnOnce = true;

	if (bWarnOnce)
	{
		int32 CompositeActorNum = 0;
		UWorld* World = nullptr;
		if (GEditor)
		{
			World = GEditor->GetEditorWorldContext().World();
		}

		if (IsValid(World))
		{
			for (TActorIterator<ACompositeActor> It(World); It; ++It)
			{
				ACompositeActor* CompositeActor = *It;
				if (IsValid(CompositeActor))
				{
					CompositeActorNum++;
				}
			}
		}

		if (CompositeActorNum > 0)
		{
			FNotificationInfo Info(LOCTEXT("FCompositeEditorModuleHoldoutCompositeCreated", "Holdout composite components are not designed to be used with the new Composite Actor. Prefer registering meshes on a plate layer directly."));
			Info.Image = FAppStyle::GetBrush(TEXT("MessageLog.Warning"));
			Info.bFireAndForget = true;
			Info.FadeOutDuration = 4.0f;
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);

			bWarnOnce = false;
		}
	}
}

void FCompositeEditorModule::RegisterTabSpawners()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

	OnTabManagerChangedDelegateHandle = LevelEditorModule.OnTabManagerChanged().AddLambda([this]()
	{
		SCompositeEditorPanel::RegisterTabSpawner();
	});
}

void FCompositeEditorModule::UnregisterTabSpawners()
{
	SCompositeEditorPanel::UnregisterTabSpawner();

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnTabManagerChanged().Remove(OnTabManagerChangedDelegateHandle);
}

void FCompositeEditorModule::RegisterCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UCompositeLayerPlate::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCompositeLayerPlateCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomClassLayout(
		UCompositeLayerShadowReflection::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCompositeLayerShadowReflectionCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomClassLayout(
			UCompositeLayerSceneCapture::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FCompositeLayerSceneCaptureCustomization::MakeInstance)
		);

	PropertyModule.RegisterCustomClassLayout(
			UCompositeLayerSingleLightShadow::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FCompositeLayerSingleLightShadowCustomization::MakeInstance)
		);

	PropertyModule.RegisterCustomClassLayout(
		ACompositeActor::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCompositeActorCustomization::MakeInstance)
	);
}

void FCompositeEditorModule::UnregisterCustomizations()
{
	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		if (FPropertyEditorModule* PropertyModule = FModuleManager::Get().GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyModule->UnregisterCustomClassLayout(ACompositeActor::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UCompositeLayerPlate::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UCompositeLayerShadowReflection::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UCompositeLayerSceneCapture::StaticClass()->GetFName());
			PropertyModule->UnregisterCustomClassLayout(UCompositeLayerSingleLightShadow::StaticClass()->GetFName());
		}
	}
}

IMPLEMENT_MODULE(FCompositeEditorModule, CompositeEditor)

#undef LOCTEXT_NAMESPACE
