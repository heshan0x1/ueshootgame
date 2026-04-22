// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"

class UAnimNextSharedVariablesEntry;

namespace UE::UAF::Editor
{

class IWorkspaceOutlinerItemDetails;

struct FVariablesOutlinerStructSharedVariablesItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	FVariablesOutlinerStructSharedVariablesItem(UAnimNextSharedVariablesEntry* InEntry);

	// Begin ISceneOutlinerTreeItem overrides
	virtual bool IsValid() const override;
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual FString GetPackageName() const override;
	// End ISceneOutlinerTreeItem overrides

	// Weak-ptr to the represented entry
	TWeakObjectPtr<UAnimNextSharedVariablesEntry> WeakEntry;
};

}
