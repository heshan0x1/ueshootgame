// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "Templates/SharedPointer.h"
#include "Containers/UnrealString.h"
#include "PropertyHandle.h"
#include "UObject/Object.h"

#define UE_API METAHUMANCAPTUREDATAEDITOR_API



class SMetaHumanCameraCombo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCameraCombo)
	{
	}
	SLATE_END_ARGS()

	typedef TSharedPtr<FString> FComboItemType;

	UE_API void Construct(const FArguments& InArgs, const TArray<TSharedPtr<FString>>* InOptionsSource, const FString* InCamera, TObjectPtr<UObject> InPropertyOwner, TSharedPtr<IPropertyHandle> InProperty);

	UE_API void HandleSourceDataChanged(class UFootageCaptureData* InFootageCaptureData, class USoundWave* InAudio, bool bInResetRanges);
	UE_API void HandleSourceDataChanged(bool bInResetRanges);

	UE_API TSharedRef<SWidget> MakeWidgetForOption(FComboItemType InOption);

	UE_API void OnSelectionChanged(FComboItemType InNewValue, ESelectInfo::Type);

	UE_API FText GetCurrentItemLabel() const;

	UE_API bool IsEnabled() const;

private:

	const FString* Camera = nullptr;
	TObjectPtr<UObject> PropertyOwner;
	TSharedPtr<IPropertyHandle> Property;
	TSharedPtr<SComboBox<FComboItemType>> Combo;
};

#undef UE_API
