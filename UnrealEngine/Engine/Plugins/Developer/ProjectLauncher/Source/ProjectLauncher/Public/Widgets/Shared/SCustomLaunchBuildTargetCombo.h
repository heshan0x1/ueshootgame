// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Model/ProjectLauncherModel.h"

#define UE_API PROJECTLAUNCHER_API

class SCustomLaunchBuildTargetCombo
	: public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectionChanged, FString );

	SLATE_BEGIN_ARGS(SCustomLaunchBuildTargetCombo)
		: _TextStyle( &FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>( "NormalText" ) )
		{}
		SLATE_EVENT(FOnSelectionChanged, OnSelectionChanged);
		SLATE_ATTRIBUTE(FString, SelectedProject);
		SLATE_ATTRIBUTE(FString, SelectedBuildTarget);
		SLATE_ATTRIBUTE(TArray<EBuildTargetType>, SupportedTargetTypes);
		SLATE_ATTRIBUTE(FSlateFontInfo, Font)
		SLATE_STYLE_ARGUMENT( FTextBlockStyle, TextStyle )
	SLATE_END_ARGS()

public:
	UE_API void Construct(	const FArguments& InArgs, TSharedRef<ProjectLauncher::FModel> InModel);

protected:
	UE_API FString GetDefaultBuildTargetName() const;

	TAttribute<TArray<EBuildTargetType>> SupportedTargetTypes;
	TAttribute<FString> SelectedProject;
	TAttribute<FString> SelectedBuildTarget;
	FOnSelectionChanged OnSelectionChanged;
	bool bShowAnyProjectOption;
	TSharedPtr<ProjectLauncher::FModel> Model;

	UE_API TSharedRef<SWidget> MakeBuildTargetSelectionWidget();

	UE_API FText GetBuildTargetName() const;
	UE_API void SetBuildTargetName(FString BuildTargetName);


};

#undef UE_API
