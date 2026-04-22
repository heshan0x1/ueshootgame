// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Fonts/SlateFontInfo.h"

#define UE_API PROJECTLAUNCHER_API

template<typename ItemType> class SComboBox;


class SCustomLaunchPlatformCombo
	: public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, TArray<FString> );

	SLATE_BEGIN_ARGS(SCustomLaunchPlatformCombo)
		: _TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{}
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged);
		SLATE_ATTRIBUTE(TArray<FString>, SelectedPlatforms);
		SLATE_ARGUMENT(bool, BasicPlatformsOnly)
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_STYLE_ARGUMENT( FTextBlockStyle, TextStyle )
	SLATE_END_ARGS()

public:
	UE_API void Construct(	const FArguments& InArgs);

protected:
	TAttribute<TArray<FString>> SelectedPlatforms;
	FOnSelectionChanged OnSelectionChanged;
	bool bBasicPlatformsOnly;

	UE_API TSharedRef<SWidget> OnGeneratePlatformListWidget( TSharedPtr<FString> Platform ) const;
	UE_API void OnPlatformSelectionChanged( TSharedPtr<FString> Platform, ESelectInfo::Type InSelectInfo );
	UE_API const FSlateBrush* GetSelectedPlatformBrush() const;
	UE_API FText GetSelectedPlatformName() const;
	TArray<TSharedPtr<FString>> PlatformsList;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PlatformsComboBox;
};

#undef UE_API
