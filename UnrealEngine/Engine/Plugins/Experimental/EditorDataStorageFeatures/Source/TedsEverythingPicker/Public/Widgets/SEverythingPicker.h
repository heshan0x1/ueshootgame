// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "Styling/SlateTypes.h"

#include "Context/TedsPickerContext.h"

class SWidgetSwitcher;
class SHorizontalBox;

namespace UE::Editor::DataStorage::Picker
{
	class SEverythingPicker : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEverythingPicker)
			{}
			SLATE_SLOT_ARGUMENT(FPickerContext, Contexts)
		SLATE_END_ARGS()

		TEDSEVERYTHINGPICKER_API void Construct(const FArguments& InArgs);

		TEDSEVERYTHINGPICKER_API void AddContext(const FPickerContext::FSlotArguments& SlotArgs);

		static FPickerContext::FSlotArguments Context()
		{
			return FPickerContext::FSlotArguments(MakeUnique<FPickerContext>());
		}

		static FPickerContext::FSlotArguments Context(FPickerContext::FSlotArguments&& InContext)
		{
			return MoveTemp(InContext);
		}
	private:

		ECheckBoxState IsContextTabSelected(int32 ContextId) const;
		void OnActiveContextChanged(ECheckBoxState State, int32 ContextId);

		int32 ActiveContextId;
		TSharedPtr<SHorizontalBox> ContextTabs;
		TSharedPtr<SWidgetSwitcher> ContextTabViews;
	};
}