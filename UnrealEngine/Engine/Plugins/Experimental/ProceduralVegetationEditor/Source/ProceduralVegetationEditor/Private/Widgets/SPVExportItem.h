// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SBoxPanel.h"
#include "Input/Reply.h"
#include <Widgets/Input/SCheckBox.h>
#include "Widgets/SCompoundWidget.h"

class SPVExportItem : public SCompoundWidget
{
	DECLARE_DELEGATE_TwoParams(FOnOutputSelectionChanged, const FString ItemName, ECheckBoxState NewState);

	SLATE_BEGIN_ARGS(SPVExportItem)
	{}
	SLATE_ARGUMENT(FText, Name)
	SLATE_ARGUMENT(bool, bIsSelected)
	SLATE_EVENT(FOnOutputSelectionChanged, OnOutputSelectionChanged)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	ECheckBoxState OnCheckBoxState() const;

	FOnOutputSelectionChanged OnOutputSelectionChanged;
private:
	bool bIsSelected;
};
