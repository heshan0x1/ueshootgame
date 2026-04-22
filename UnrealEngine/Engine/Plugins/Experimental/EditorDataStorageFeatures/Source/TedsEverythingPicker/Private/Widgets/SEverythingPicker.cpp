// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SEverythingPicker.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#define LOCTEXT_NAMESPACE "SEverythingPicker"

namespace UE::Editor::DataStorage::Picker
{
	TAutoConsoleVariable<bool> CVarPickerSystemEnabled(
		TEXT("TEDS.Feature.PickerEnabled"),
		false, // Defaulting to false until we hit feature parity (search bar, custom filtering, etc.)
		TEXT("If true, enables uses of TED's EverythingPicker for testing."));

	void SEverythingPicker::Construct(const FArguments& InArgs)
	{
		ContextTabs = SNew(SHorizontalBox);
		ContextTabViews = SNew(SWidgetSwitcher);

		static const FVector2D PickerWindowSize(350.0f, 300.0f);

		FMenuBuilder MenuBuilder(true, nullptr);

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("PickerView", "View"));
		{
			TSharedPtr<SWidget> MenuContent;

			for (const FPickerContext::FSlotArguments& ContextArgs : InArgs._Contexts)
			{
				AddContext(ContextArgs);
			}

			MenuContent =
				SNew(SBox)
				.WidthOverride(static_cast<float>(PickerWindowSize.X))
				.HeightOverride(static_cast<float>(PickerWindowSize.Y))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8, 2)
					[
						ContextTabs.ToSharedRef()
					]
					+ SVerticalBox::Slot()
					.VAlign(VAlign_Fill)
					.FillHeight(1.0)
					.Padding(3)
					[
						ContextTabViews.ToSharedRef()
					]
				];

			MenuBuilder.AddWidget(MenuContent.ToSharedRef(), FText::GetEmpty(), true);
		}
		MenuBuilder.EndSection();

		ChildSlot
			[
				MenuBuilder.MakeWidget()
			];
	}

	void SEverythingPicker::AddContext(const FPickerContext::FSlotArguments& SlotArgs)
	{
		int32 NextContextId = ContextTabViews->GetNumWidgets();

		ContextTabs->AddSlot()
		.AutoWidth()
		.Padding(4)
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckBox"))
			.IsChecked(this, &SEverythingPicker::IsContextTabSelected, NextContextId)
			.OnCheckStateChanged(this, &SEverythingPicker::OnActiveContextChanged, NextContextId)
			[
				SNew(STextBlock)
				.Text(SlotArgs._Label)
			]
		];

		TSharedPtr<SWidget> ContextWidget = SlotArgs.GetAttachedWidget();
		if (ContextWidget)
		{
			ContextTabViews->AddSlot()
			[
				ContextWidget.ToSharedRef()
			];
		}
		else
		{
			ContextTabViews->AddSlot()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EmptyContextViewText", "Empty View"))
				]
			];
		}
	}

	ECheckBoxState SEverythingPicker::IsContextTabSelected(int32 ContextId) const
	{
		return (ContextId == ActiveContextId) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	void SEverythingPicker::OnActiveContextChanged(ECheckBoxState State, int32 ContextId)
	{
		if (State != ECheckBoxState::Checked)
		{
			return;
		}

		if (ContextId < 0 || ContextId >= ContextTabViews->GetNumWidgets())
		{
			return;
		}

		ActiveContextId = ContextId;
		ContextTabViews->SetActiveWidgetIndex(ActiveContextId);
	}
}

#undef LOCTEXT_NAMESPACE // "SEverythingPicker"