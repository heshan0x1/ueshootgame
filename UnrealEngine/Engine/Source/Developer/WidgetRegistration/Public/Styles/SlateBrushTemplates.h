// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"
#include "Styling/StyleColors.h"

#define UE_API WIDGETREGISTRATION_API

/** A const FSlateBrush* Factory */
struct FSlateBrushTemplates
{
	// Images
	static UE_API const FSlateBrush* DragHandle();
	static UE_API const FSlateBrush* ThinLineHorizontal();

	// Colors
	static UE_API const FSlateBrush* Transparent();
	static UE_API const FSlateBrush* Panel();
	static UE_API const FSlateBrush* Recessed();

	/**
	 * gets a const FSlateBrush* with the color Color
	 *
	 *  @param the EStyleColor we need the slate brush for
	 */
	UE_API const FSlateBrush* GetBrushWithColor(EStyleColor Color);

	/**
	 * gets the FSlateBrushTemplates singleton
	 */
	static UE_API FSlateBrushTemplates& Get();

	/** the map of EStyleColor to const FSlateBrush */
	TMap<EStyleColor, const FSlateBrush> EStyleColorToSlateBrushMap;
};

#undef UE_API
