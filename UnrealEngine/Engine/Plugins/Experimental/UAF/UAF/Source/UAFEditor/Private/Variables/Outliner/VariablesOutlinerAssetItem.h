// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerTreeItem.h"
#include "UObject/SoftObjectPtr.h"

class UAnimNextRigVMAsset;

namespace UE::UAF::Editor
{

class IWorkspaceOutlinerItemDetails;

struct FVariablesOutlinerAssetItem : ISceneOutlinerTreeItem
{
	static const FSceneOutlinerTreeItemType Type;
	
	struct FItemData
	{
		const TSoftObjectPtr<UAnimNextRigVMAsset>& Asset;
		const int32 SortValue;
		const TArrayView<const FSoftObjectPath> ImplementedSharedVariablesPaths;
	};

	FVariablesOutlinerAssetItem(const FItemData& InItemData);
	virtual ~FVariablesOutlinerAssetItem() override = default;

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

	FReply OnSharedVariableWidgetMouseUp(const FGeometry&, const FPointerEvent& PointerEvent, const FSoftObjectPath ClickedSharedVariablesPath) const;
	FReply OnRemoveSharedVariable(const FSoftObjectPath ClickedSharedVariablesPath) const;

	// Soft ptr to the underlying asset, which may not be loaded yet
	TSoftObjectPtr<UAnimNextRigVMAsset> SoftAsset;

	int32 SortValue;

	TArray<const FSoftObjectPath> SharedVariableSourcePaths;

	TSharedPtr<class SVariablesOutlinerAssetLabel> AssetLabel;
};

}
