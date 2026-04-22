// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class STableViewBase;
template <typename> class STreeView;
class UAbstractSkeletonSetBinding;

namespace UE::UAF::Sets
{
	class SSetsSkeletonTree : public SCompoundWidget
	{
	public:
		struct FTreeItem
		{
			FName BoneName;
			FName BoundSet;

			TWeakPtr<FTreeItem> Parent;
			TArray<TSharedPtr<FTreeItem>> Children;
		};

		SLATE_BEGIN_ARGS(SSetsSkeletonTree) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UAbstractSkeletonSetBinding>, SetBinding)
			SLATE_EVENT(FSimpleDelegate, OnTreeRefreshed)
		SLATE_END_ARGS()

		virtual void Construct(const FArguments& InArgs);

		void RepopulateTreeData();

		void SetSetBinding(TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding);

	private:
		TSharedRef<ITableRow> TreeView_OnGenerateRow(TSharedPtr<FTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
		void TreeView_OnGetChildren(TSharedPtr<FTreeItem> InParent, TArray<TSharedPtr<FTreeItem>>& OutChildren);

		TArray<TSharedPtr<FTreeItem>> GetAllTreeItems();

	private:
		TWeakObjectPtr<UAbstractSkeletonSetBinding> SetBinding;

		TArray<TSharedPtr<FTreeItem>> RootItems;

		TSharedPtr<STreeView<TSharedPtr<FTreeItem>>> TreeView;

		FSimpleDelegate OnTreeRefreshed;

		bool bRepopulating = false;
	};

}