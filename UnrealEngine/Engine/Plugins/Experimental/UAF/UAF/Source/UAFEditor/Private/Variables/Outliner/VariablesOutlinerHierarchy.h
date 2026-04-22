// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"

class UAnimNextRigVMAssetEditorData;
class UAnimNextRigVMAsset;

namespace UE::UAF::Editor
{

class FVariablesOutlinerHierarchy : public ISceneOutlinerHierarchy
{
public:
	FVariablesOutlinerHierarchy(ISceneOutlinerMode* Mode);
	FVariablesOutlinerHierarchy(const FVariablesOutlinerHierarchy&) = delete;
	FVariablesOutlinerHierarchy& operator=(const FVariablesOutlinerHierarchy&) = delete;

	// Begin ISceneOutlinerHierarchy overrides
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override {}
	virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate = false) override;
	// End ISceneOutlinerHierarchy overrides
protected:
	void ProcessAsset(const TSoftObjectPtr<UAnimNextRigVMAsset>& InSoftAsset, int32 Depth, TArray<FSceneOutlinerTreeItemPtr>& OutItems, TArray<const FSoftObjectPath>& OutSharedVariableSourcePaths) const;

	mutable TSet<TWeakObjectPtr<const UAnimNextRigVMAsset>> WeakProcessedAssets;
	mutable TSet<TWeakObjectPtr<const UScriptStruct>> WeakProcessedStructs;
};

}