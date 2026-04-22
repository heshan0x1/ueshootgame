// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Parameters/BuildStorageToolParameters.h"

class SBuildStorageToolHelpWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBuildStorageToolHelpWidget) {}
		SLATE_ATTRIBUTE(const FBuildStorageToolParameters*, ToolParameters)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
private:
	const FBuildStorageToolParameters* ToolParameters;
};