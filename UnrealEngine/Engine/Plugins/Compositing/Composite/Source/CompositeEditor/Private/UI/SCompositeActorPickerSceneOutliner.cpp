// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCompositeActorPickerSceneOutliner.h"

#include "ActorMode.h"
#include "ActorTreeItem.h"
#include "EngineUtils.h"
#include "ISceneOutlinerColumn.h"
#include "ISceneOutlinerMode.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "WorldTreeItem.h"
#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SCompositeActorPickerTable"

/** Custom column for the scene outliner that displays a checkbox to allow users to select multiple actors quickly */
class FCompositeActorPickerSelectedColumn : public ISceneOutlinerColumn
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnGetActorSelected, AActor*);
	DECLARE_DELEGATE_TwoParams(FOnActorSelected, AActor*, bool /* bIsSelected */);
	
public:
	FCompositeActorPickerSelectedColumn(ISceneOutliner& InSceneOutliner)
	{
		SceneOutliner = StaticCastSharedRef<ISceneOutliner>(InSceneOutliner.AsShared());
	}

	static FName GetID() { return "IsSelected"; }

	// ISceneOutlinerColumn interface
	virtual FName GetColumnID() override
	{
		return GetID();
	}
	
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override
	{
		return SHeaderRow::Column(GetColumnID())
			.FixedWidth(24.0f)
			.DefaultLabel(FText::GetEmpty())
			.HAlignHeader(HAlign_Left)
			.VAlignHeader(VAlign_Center)
			.HAlignCell(HAlign_Center)
			.VAlignCell(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([] { return ECheckBoxState::Checked; })
			];
	}

	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef InTreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override
	{
		if (InTreeItem->IsA<FActorTreeItem>())
		{
			return SNew(SCheckBox)
				.IsChecked(this, &FCompositeActorPickerSelectedColumn::GetTreeItemSelectedCheckState, InTreeItem)
				.OnCheckStateChanged(this, &FCompositeActorPickerSelectedColumn::OnTreeItemSelectedCheckStateChanged, InTreeItem);
		}

		return SNullWidget::NullWidget;
	}
	// End of ISceneOutlinerColumn interface
	
private:
	ECheckBoxState GetTreeItemSelectedCheckState(FSceneOutlinerTreeItemRef InTreeItem) const
	{
		if (FActorTreeItem* ActorTreeItem = InTreeItem->CastTo<FActorTreeItem>())
		{
			if (!OnGetActorSelected.IsBound())
			{
				return ECheckBoxState::Unchecked;
			}
			
			if (TStrongObjectPtr<AActor> PinnedActor = ActorTreeItem->Actor.Pin())
			{
				return OnGetActorSelected.Execute(PinnedActor.Get()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}

			return ECheckBoxState::Unchecked;
		}
		
		return ECheckBoxState::Unchecked;
	}

	void OnTreeItemSelectedCheckStateChanged(ECheckBoxState InNewCheckState, FSceneOutlinerTreeItemRef InTreeItem)
	{
		if (FActorTreeItem* ActorTreeItem = InTreeItem->CastTo<FActorTreeItem>())
		{
			if (TStrongObjectPtr<AActor> PinnedActor = ActorTreeItem->Actor.Pin())
			{
				OnActorSelected.ExecuteIfBound(PinnedActor.Get(), InNewCheckState == ECheckBoxState::Checked);
			}
		}
	}
	
private:
	TWeakPtr<ISceneOutliner> SceneOutliner = nullptr;

public:
	FOnGetActorSelected OnGetActorSelected;
	FOnActorSelected OnActorSelected;
};

void SCompositeActorPickerSceneOutliner::Construct(const FArguments& InArgs, const FCompositeActorPickerListRef& InActorListRef)
{
	ActorListRef = InActorListRef;
	OnActorListChanged = InArgs._OnActorListChanged;
	
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	FSceneOutlinerInitializationOptions InitOptions;
	{
		InitOptions.bShowHeaderRow = true;
		InitOptions.bShowSearchBox = true;
		InitOptions.bShowCreateNewFolder = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;
		InitOptions.Filters = InArgs._SceneOutlinerFilters;

		InitOptions.ColumnMap.Add(FCompositeActorPickerSelectedColumn::GetID(), FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			0,
			FCreateSceneOutlinerColumn::CreateLambda([this](ISceneOutliner& Outliner)
			{
				TSharedPtr<FCompositeActorPickerSelectedColumn> Column = MakeShared<FCompositeActorPickerSelectedColumn>(Outliner);
				Column->OnGetActorSelected.BindSP(this, &SCompositeActorPickerSceneOutliner::OnGetActorSelected);
				Column->OnActorSelected.BindSP(this, &SCompositeActorPickerSceneOutliner::OnActorSelected);
				
				return Column.ToSharedRef();
			})
		));
			
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 1));
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 2));
	}
	
	ChildSlot
	[
		SNew(SBox)
		.MaxDesiredHeight(400.0f)
		.WidthOverride(300.0f)
		[
			SceneOutlinerModule.CreateActorPicker(
				InitOptions,
				FOnActorPicked::CreateLambda([&](AActor* Actor)
				{
					OnActorSelected(Actor, true);
					FSlateApplication::Get().DismissAllMenus();
				})
			)
		]
	];
}

bool SCompositeActorPickerSceneOutliner::OnGetActorSelected(AActor* InActor)
{
	if (!ActorListRef.IsValid())
	{
		return false;
	}

	return ActorListRef.ActorList->Contains(InActor);
}

void SCompositeActorPickerSceneOutliner::OnActorSelected(AActor* InActor, bool bIsSelected)
{
	if (!ActorListRef.IsValid())
	{
		return;
	}
	
	TStrongObjectPtr<UObject> PinnedListOwner = ActorListRef.ActorListOwner.Pin();
	
	const bool bIsAdding = bIsSelected && !ActorListRef.ActorList->Contains(InActor);
	const bool bIsRemoving = !bIsSelected && ActorListRef.ActorList->Contains(InActor);

	if (!bIsAdding && !bIsRemoving)
	{
		return;
	}
	
	FScopedTransaction SelectActorTransaction(LOCTEXT("SelectActorTransaction", "Select Actor"));
	PinnedListOwner->Modify();

	ActorListRef.NotifyPreEditChange();

	EPropertyChangeType::Type ChangeType = EPropertyChangeType::Unspecified;
	if (bIsAdding)
	{
		ActorListRef.ActorList->Add(InActor);
		ChangeType = EPropertyChangeType::ArrayAdd;
	}
	else if (bIsRemoving)
	{
		ActorListRef.ActorList->Remove(InActor);
		ChangeType = EPropertyChangeType::ArrayRemove;
	}

	ActorListRef.NotifyPostEditChangeList(ChangeType);
	OnActorListChanged.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE