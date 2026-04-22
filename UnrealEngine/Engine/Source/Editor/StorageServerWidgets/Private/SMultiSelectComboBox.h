// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
#include "Containers/ObservableArray.h"

DECLARE_DELEGATE(FOnMultiSelectComboBoxCheckedValuesChanged)

class SMultiSelectComboBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMultiSelectComboBox) {}
		SLATE_ARGUMENT(TArray<TSharedPtr<FString>>*, SelectValues)
		SLATE_EVENT(FOnMultiSelectComboBoxCheckedValuesChanged, OnCheckedValuesChanged)
	SLATE_END_ARGS()

	virtual ~SMultiSelectComboBox();

	void Construct(const FArguments& InArgs);

	bool IsChecked(FStringView Value) const;

private:
	TSharedRef<class SWidget> GenerateMenuItems();

	void OnSelectedChangedFromMultiselect(TSharedPtr<FString> Value);

	FOnMultiSelectComboBoxCheckedValuesChanged OnCheckedValuesChanged;
	TArray<FString> CheckedValues;
	TArray<TSharedPtr<FString>>* SelectValues;
};