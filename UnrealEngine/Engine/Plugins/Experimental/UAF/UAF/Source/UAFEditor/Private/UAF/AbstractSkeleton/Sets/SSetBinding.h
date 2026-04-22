// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimData/AttributeIdentifier.h"
#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetCollection.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class FUICommandList;
class SSearchBox;
class UAbstractSkeletonSetBinding;

namespace UE::UAF::Sets
{
	class SSetBinding : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
	{
		SLATE_BEGIN_ARGS(SSetBinding) {}
			SLATE_EVENT(FSimpleDelegate, OnTreeRefreshed)
		SLATE_END_ARGS()

	public:
		struct ITreeItem
		{
			ITreeItem(SSetBinding& InSetBindingWidget)
				: SetBindingWidget(InSetBindingWidget)
			{
			}
			
			virtual ~ITreeItem() = default;
			virtual FName GetType() const = 0;
			virtual void GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren) = 0;
			virtual TSharedRef<ITableRow> GenerateRow(const TSharedRef<STableViewBase>& OwnerTable) = 0;
			virtual bool GenerateContextMenu(FMenuBuilder& Menu) = 0;
			
			SSetBinding& SetBindingWidget;
		};

		struct FTreeItem_Set : public ITreeItem
		{
			FTreeItem_Set(SSetBinding& InSetBindingWidget, FAbstractSkeletonSet InSet)
				: ITreeItem(InSetBindingWidget)
				, Set(InSet)
			{
			}
			
			virtual ~FTreeItem_Set() override = default;
			
			static FName StaticGetType() { return FName("Set"); }
			
			virtual FName GetType() const override { return StaticGetType(); }
			virtual void GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren) override;
			virtual TSharedRef<ITableRow> GenerateRow(const TSharedRef<STableViewBase>& OwnerTable) override;
			virtual bool GenerateContextMenu(FMenuBuilder& Menu) override;

			FAbstractSkeletonSet Set;
			TArray<TSharedPtr<ITreeItem>> Children;
		};

		struct FTreeItem_Bone : public ITreeItem
		{
			FTreeItem_Bone(SSetBinding& InSetBindingWidget, FAbstractSkeleton_BoneBinding InBinding)
				: ITreeItem(InSetBindingWidget)
				, Binding(InBinding)
			{
			}
			
			virtual ~FTreeItem_Bone() override = default;

			static FName StaticGetType() { return FName("Bone"); }
			
			virtual FName GetType() const override { return StaticGetType(); }
			virtual void GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren) override;
			virtual TSharedRef<ITableRow> GenerateRow(const TSharedRef<STableViewBase>& OwnerTable) override;
			virtual bool GenerateContextMenu(FMenuBuilder& Menu) override;

			FAbstractSkeleton_BoneBinding Binding;
		};
		
		struct FTreeItem_Attribute : public ITreeItem
		{
			FTreeItem_Attribute(SSetBinding& InSetBindingWidget, FAbstractSkeleton_AttributeBinding InBinding)
				: ITreeItem(InSetBindingWidget)
				, Binding(InBinding)
			{
			}
			
			virtual ~FTreeItem_Attribute() override = default;

			static FName StaticGetType() { return FName("Attribute"); }
			
			virtual FName GetType() const override { return StaticGetType(); }
			virtual void GetChildren(TArray<TSharedPtr<ITreeItem>>& OutChildren) override;
			virtual TSharedRef<ITableRow> GenerateRow(const TSharedRef<STableViewBase>& OwnerTable) override;
			virtual bool GenerateContextMenu(FMenuBuilder& Menu) override;

			FAbstractSkeleton_AttributeBinding Binding;
		};

		void Construct(const FArguments& InArgs, TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding);

		/** FSelfRegisteringEditorUndoClient interface */
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override;

		void RepopulateTreeData();

		void SetSetBinding(TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding);

	private:
		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

		TSharedRef<ITableRow> TreeView_OnGenerateRow(TSharedPtr<ITreeItem> InItem, const TSharedRef<STableViewBase>& OwnerTable);

		void TreeView_OnGetChildren(TSharedPtr<ITreeItem> InItem, TArray<TSharedPtr<ITreeItem>>& OutChildren);

		TSharedPtr<SWidget> TreeView_OnContextMenuOpening();

		void ExpandAllTreeItems();

		void BindCommands();

		TArray<TSharedPtr<ITreeItem>> GetAllTreeItems();

	public:
		TWeakObjectPtr<UAbstractSkeletonSetBinding> SetBinding;

	private:
		TSharedPtr<STreeView<TSharedPtr<ITreeItem>>> TreeView;

		TArray<TSharedPtr<ITreeItem>> RootItems;

		TSharedPtr<SSearchBox> SearchBox;

		TSharedPtr<FUICommandList> CommandList;

		FSimpleDelegate OnTreeRefreshed;

		bool bRepopulating = false;
	};

}
