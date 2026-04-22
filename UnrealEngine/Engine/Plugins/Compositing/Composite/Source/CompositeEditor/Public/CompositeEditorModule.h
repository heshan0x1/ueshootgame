// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelEditor.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class IConcertClientWorkspace;

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogCompositeEditor, Log, All);

class FCompositeEditorModule : public IModuleInterface
{
public:
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Called after the engine has initialized. */
	void OnPostEngineInit();

	/** Called when an Actor has been added to a level. */
	void OnLevelActorAdded(AActor* InActor);

	/** Callback to trigger warnings when an holdout composite component is created with a composite actor. */
	void TriggerHoldoutCompositeWarning(const class UHoldoutCompositeComponent* InComponent);

	/** Register tab spawners. */
	void RegisterTabSpawners();

	/** Unregister tab spawners. */
	void UnregisterTabSpawners();

	void RegisterCustomizations();
	void UnregisterCustomizations();

	/** Workspace startup callback. */
	void HandleWorkspaceStartup(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace);
	
	/** Workspace shutdown callback. */
	void HandleWorkspaceShutdown(const TSharedPtr<IConcertClientWorkspace>& WorkspaceShuttingDown);

	/** Workspace sync completed callback. */
	void HandleFinalizeWorkspaceSyncCompleted();

private:
	/** Cached concert sync client. */
	TSharedPtr<class IConcertSyncClient> ConcertSyncClient;

	/** Tab manager change delegate. */
	FDelegateHandle OnTabManagerChangedDelegateHandle;

	/** Holds a weak pointer to the current workspace. */
	TWeakPtr<IConcertClientWorkspace> Workspace;
};
