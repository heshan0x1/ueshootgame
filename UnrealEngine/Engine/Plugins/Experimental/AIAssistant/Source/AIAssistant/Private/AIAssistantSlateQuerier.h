// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once


#include "Layout/WidgetPath.h"


//
// UE::AIAssistant::SlateQuerier
//


namespace UE::AIAssistant::SlateQuerier
{
	/**
	 * Get the widget path under the cursor
	 */
	FWidgetPath GetWidgetPathUnderCursor();

	/**
	 * Initiates an AI Assistant query to describe a Slate widget.
	 */
	void QueryAIAssistantAboutSlateWidget(const FWidgetPath& InWidgetPath);
};
