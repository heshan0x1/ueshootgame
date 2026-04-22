// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IWorkspaceOutlinerItemDetails.h"

class UAnimNextRigVMAssetEditorData;
enum class EAnimNextEditorDataNotifType : uint8;

namespace UE::UAF::Editor
{

class FAnimNextAssetItemDetails : public UE::Workspace::IWorkspaceOutlinerItemDetails
{
public:
	FAnimNextAssetItemDetails() = default;

	UAFEDITOR_API virtual const FSlateBrush* GetItemIcon(const FWorkspaceOutlinerItemExport& Export) const override;
	UAFEDITOR_API virtual bool HandleDoubleClick(const FToolMenuContext& ToolMenuContext) const override;

	static void RegisterToolMenuExtensions();
	static void UnregisterToolMenuExtensions();
};

}
