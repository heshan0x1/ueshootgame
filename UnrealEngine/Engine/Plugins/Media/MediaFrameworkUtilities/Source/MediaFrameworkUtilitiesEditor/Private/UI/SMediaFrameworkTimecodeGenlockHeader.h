// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class UMediaProfile;

/**
 * Widget that displays the timecode and genlock status for a media profile that can be displayed in toolbars or details panel headers
 */
class SMediaFrameworkTimecodeGenlockHeader : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMediaFrameworkTimecodeGenlockHeader) { }
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

private:
	/** Gets the text to display in the tooltip for the widget */
	FText GetTooltipText() const;

	/** Gets the text to display for the timecode */
	FText GetTimecodeText() const;

	/** Gets the icon to display for the genlock */
	const FSlateBrush* GetGenlockIcon() const;

	/** Gets the text to display for the genlock */
	FText GetGenlockText() const;
};
