// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"

class UAnimNextRigVMAsset;

namespace UE::UAF::Editor
{

struct FVariablesOutlinerCategoryItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;

	struct FItemData
	{
		UAnimNextRigVMAsset* InOwner;
		FStringView InCategoryName;
		FStringView InParentCategoryName;
		FStringView InCategoryPath;
	};

	FVariablesOutlinerCategoryItem(const FItemData& InItemData);

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
	
	TWeakObjectPtr<UAnimNextRigVMAsset> WeakOwner;
	FString CategoryName;
	FString ParentCategoryName;
	FString CategoryPath;
	int32 SortValue = 0;
};

}
