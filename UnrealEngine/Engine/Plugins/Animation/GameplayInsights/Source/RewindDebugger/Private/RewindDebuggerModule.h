// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRewindDebuggerModule.h"
#include "RewindDebuggerCamera.h"
#include "RewindDebuggerAnimation.h"

class SDockTab;
class SRewindDebugger;
class SRewindDebuggerDetails;

class FRewindDebuggerModule : public IRewindDebuggerModule
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual FName GetMainTabName() const override;
	virtual FName GetDetailsTabName() const override;
	virtual TSharedRef<SDockTab> SpawnRewindDebuggerTab(const FSpawnTabArgs& SpawnTabArgs) override;
	virtual TSharedRef<SDockTab> SpawnRewindDebuggerDetailsTab(const FSpawnTabArgs& SpawnTabArgs) override;

	static const FName MainTabName;
	static const FName DetailsTabName;
	static const FName MainMenuName;
	static const FName TrackContextMenuName;

private:
	TSharedPtr<SRewindDebugger> RewindDebuggerWidget;
	TSharedPtr<SRewindDebuggerDetails> RewindDebuggerDetailsWidget;

	FRewindDebuggerCamera RewindDebuggerCameraExtension;
	FRewindDebuggerAnimation RewindDebuggerAnimationExtension;

	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle LevelEditorLayoutExtensionHandle;
};
