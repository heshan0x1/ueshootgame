// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimData/AttributeIdentifier.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#include "SAttributesList.generated.h"

class UAbstractSkeletonSetBinding;

namespace UE::UAF::Sets
{

	class SAttributesList : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAttributesList) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UAbstractSkeletonSetBinding>, SetBinding)
			SLATE_EVENT(FSimpleDelegate, OnListRefreshed)
		SLATE_END_ARGS()

		virtual void Construct(const FArguments& InArgs);

		void RepopulateListData();

		void SetSetBinding(TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding);

	public:
		struct FColumns
		{
			static FName SetId;
			static FName AttributeId;
		};

		struct FMenus
		{
			static FName AddAttributeId;
		};

		struct FListItem
		{
			FListItem(const FName InSetName, const FAnimationAttributeIdentifier& InAttribute);

			FAnimationAttributeIdentifier Attribute;
			FName SetName;
		};

		using FListItemPtr = TSharedPtr<FListItem>;

	private:
		TSharedRef<ITableRow> ListView_OnGenerateRow(FListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
		
		TSharedRef<SWidget> CreateAddAttributeWidget();

		void RegisterMenus();

	private:
		TArray<FListItemPtr> ListItems;

		TSharedPtr<SListView<FListItemPtr>> ListView;

		TWeakObjectPtr<UAbstractSkeletonSetBinding> SetBinding;

		FSimpleDelegate OnListRefreshed;

		bool bRepopulating = false;
	};

}

UCLASS()
class UAttributesListMenuContext : public UObject
{
	GENERATED_BODY()
	
public:
	TWeakPtr<UE::UAF::Sets::SAttributesList> SetBindingWidget;
};