// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"

class UAnimNextSharedVariablesEntry;
class UAnimNextVariableEntry;

namespace UE::UAF::Editor
{

struct FVariablesOutlinerEntryItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	FVariablesOutlinerEntryItem(UAnimNextVariableEntry* InEntry);
	FVariablesOutlinerEntryItem(const FProperty* InProperty);

	// Begin ISceneOutlinerTreeItem overrides
	virtual bool IsValid() const override;
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual FString GetPackageName() const override;
	// End ISceneOutlinerTreeItem overrides

	// Renames the item to the specified name
	void Rename(const FText& InNewName) const;

	// Validates the new item name
	bool ValidateName(const FText& InNewName, FText& OutErrorMessage) const;

	bool HasStructOwner() const { return bStructOwner; }

	// Ptr to the underlying entry if this variable came from an asset
	TWeakObjectPtr<UAnimNextVariableEntry> WeakEntry;

	// The data interface entry this entry is from, if any
	TWeakObjectPtr<const UAnimNextSharedVariablesEntry> WeakSharedVariablesEntry;

	// Ptr to the property if this variable came from a struct
	TFieldPath<const FProperty> PropertyPath;

	bool bStructOwner = false;

	int32 SortValue = 0;
};

}
