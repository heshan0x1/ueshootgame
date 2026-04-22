// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "DetailLayoutBuilder.h"


/** Widget used to select an enum value based on groupings. */
template<typename TEnum>
class SEnumGroupsWidget : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, TEnum)
	
	struct EnumGroup
	{
		FText GroupName;
		TArray<TEnum> EnumValues;
	};
	using EnumGroupList = TArray<EnumGroup>;

	SLATE_BEGIN_ARGS(SEnumGroupsWidget<TEnum>)
		: _InitiallySelectedItem(static_cast<TEnum>(0))
		, _EnumGroups(EnumGroupList())
		{}

		SLATE_ARGUMENT(TEnum, InitiallySelectedItem)

		SLATE_ARGUMENT(EnumGroupList, EnumGroups)

		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs)
	{
		SelectedItem = InArgs._InitiallySelectedItem;
		EnumGroups = InArgs._EnumGroups;
		OnSelectionChanged = InArgs._OnSelectionChanged;
	
		ChildSlot
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &SEnumGroupsWidget::GenerateMenu)
			.VAlign(VAlign_Center)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([this]() {
					 return StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(SelectedItem));
				})
				.ToolTipText_Lambda([this]() {
					 return StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(SelectedItem));
				})
			]
		];
	}

private:
	TSharedRef<SWidget> GenerateMenu()
	{
		FMenuBuilder MenuBuilder(true, nullptr);
		for (const EnumGroup& EnumGroup : EnumGroups)
		{
			if (EnumGroup.EnumValues.Num() == 1)
			{
				TEnum SingleEnum = EnumGroup.EnumValues[0];			
				MenuBuilder.AddMenuEntry(
					StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(SingleEnum)),
					StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(SingleEnum)),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SEnumGroupsWidget::OnEnumSelected, SingleEnum)));
			}
			else
			{
				MenuBuilder.AddSubMenu(
					EnumGroup.GroupName,
					EnumGroup.GroupName,
					FNewMenuDelegate::CreateSPLambda(this, [this, EnumGroup](FMenuBuilder& SubMenuBuilder)
					{
						for (TEnum Enum : EnumGroup.EnumValues)
						{
							SubMenuBuilder.AddMenuEntry(
								StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(Enum)),
								StaticEnum<TEnum>()->GetDisplayNameTextByValue(static_cast<int64>(Enum)),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateSP(this, &SEnumGroupsWidget::OnEnumSelected, Enum)));
						}
					})
				);
			}
		}
		return MenuBuilder.MakeWidget();
	}

	void OnEnumSelected(TEnum SelectedEnum)
	{
		SelectedItem = SelectedEnum;
		OnSelectionChanged.ExecuteIfBound(SelectedEnum);
	}

private:	
	TEnum SelectedItem;
	EnumGroupList EnumGroups;
	FOnSelectionChanged OnSelectionChanged;
};

#endif
