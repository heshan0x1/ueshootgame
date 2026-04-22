// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::ControlRigEditor
{
class STabButton;
	
struct FTabEntry
{
	/** Content to display in the button */
	TAlwaysValidWidget ButtonContent;

	/** Min widhth you want the button slot to have when the panel is resized */
	TAttribute<float> MinButtonSlotWidth = 60.f;

	/** Invoked when the tab is opened */
	FSimpleDelegate OnTabSelected;
};

/** Manages multiple STabButtons, making sure exactly 1 is active at a time. */
class STabArea : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(STabArea)
		: _ActiveTabIndex(0)
		, _Padding(3.f)
		{}
		/** The tabs to create */
		SLATE_ARGUMENT(TArray<FTabEntry>, Tabs)
		/** The tab that should be active by default. */
		SLATE_ARGUMENT(int32, ActiveTabIndex)
		
		/** Padding between buttons */
		SLATE_ARGUMENT(FMargin, Padding)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Informs the owning view has manually changed to the content displayed by ButtonIndex. Make the button appear selected. */
	void SetButtonActivated(int32 ButtonIndex);

private:

	/** The buttons in this area. */
	TArray<TSharedRef<STabButton>> TabButtons;

	/** Called when the button at TabButtons[ButtonIndex] is activated. */
	void OnButtonActivated(int32 ButtonIndex);
};
}

