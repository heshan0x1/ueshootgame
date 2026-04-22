// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlCVars.h"

namespace SourceControlCVars
{
	TAutoConsoleVariable<bool> CVarSourceControlEnableRevertFromSceneOutliner(
		TEXT("SourceControl.Revert.EnableFromSceneOutliner"),
		false,
		TEXT("Allows a SourceControl 'Revert' operation to be triggered from the SceneOutliner."));

	TAutoConsoleVariable<bool> CVarSourceControlEnableRevertFromSubmitWidget(
		TEXT("SourceControl.Revert.EnableFromSubmitWidget"),
		false,
		TEXT("Allows a SourceControl 'Revert' operation to be triggered from the SubmitWidget."));

	TAutoConsoleVariable<bool> CVarSourceControlEnableRevertUnsaved(
		TEXT("SourceControl.RevertUnsaved.Enable"),
		false,
		TEXT("Allows a SourceControl 'Revert' operation to be triggered on an unsaved asset."));

	TAutoConsoleVariable<bool> CVarSourceControlEnableLoginDialogModal(
		TEXT("SourceControl.LoginDialog.ForceModal"),
		false,
		TEXT("Forces the SourceControl 'Login Dialog' to always be a modal dialog."));

	TAutoConsoleVariable<bool> CVarSourceControlEnableWorldReload(
		TEXT("SourceControl.EnableWorldReload"),
		false,
		TEXT("Allows a SourceControl operation to perform a world reload."));
}