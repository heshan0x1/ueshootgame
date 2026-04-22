// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlotBase.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ITedsTableViewer.h"
#include "TedsQueryStackInterfaces.h"

namespace UE::Editor::DataStorage
{
	class FPickerContext : public TSlotBase<FPickerContext>
	{
	public:
		using TSlotBase<FPickerContext>::TSlotBase;

		SLATE_SLOT_BEGIN_ARGS(FPickerContext, TSlotBase<FPickerContext>)
			SLATE_ARGUMENT(FText, Label)
		SLATE_SLOT_END_ARGS()
	};
}
