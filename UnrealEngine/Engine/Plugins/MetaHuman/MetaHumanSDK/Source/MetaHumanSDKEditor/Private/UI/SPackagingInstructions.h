// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"

namespace UE::MetaHuman
{
/**
 * Widget to display packaging instructions when no item is selectable in MetaHuman Manager
 */
class SPackagingInstructions final : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SPackagingInstructions)
		{
		}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
}
