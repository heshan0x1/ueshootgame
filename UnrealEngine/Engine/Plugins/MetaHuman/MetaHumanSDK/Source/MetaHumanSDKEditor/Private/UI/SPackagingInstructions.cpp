// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPackagingInstructions.h"

#include "MetaHumanStyleSet.h"

#include "Components/VerticalBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PackagingInstructions"

namespace UE::MetaHuman
{
void SPackagingInstructions::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.PagePadding"))
		.BorderImage(FMetaHumanStyleSet::Get().GetBrush("Instructions.Background"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.ItemPadding"))
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(FMetaHumanStyleSet::Get().GetMargin("MetaHumanManager.IconMargin"))
				.AutoWidth()
				[
					SNew(SImage)
					.Image(FMetaHumanStyleSet::Get().GetBrush("ItemDetails.DefaultIcon"))
				]
				+ SHorizontalBox::Slot()
				.FillContentWidth(1)
				[
					SNew(STextBlock)
					.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.TitleFont"))
					.Text(LOCTEXT("NoAssetHeader", "MetaHuman Manager"))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
			+ SVerticalBox::Slot()
			.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.ItemPadding"))
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FMetaHumanStyleSet::Get().GetFontStyle("Instructions.DefaultFont"))
				.Text(LOCTEXT("NoAssetIntro", "Use MetaHuman Manager to verify and package MetaHuman assets to upload and distribute on the Fab Marketplace."))
			]
			+ SVerticalBox::Slot()
			.Padding(FMetaHumanStyleSet::Get().GetFloat("Instructions.ItemPadding"))
			.FillContentHeight(1)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			[
				SNew(SButton)
				.OnClicked_Lambda([]()
				{
					FPlatformProcess::LaunchURL(TEXT("https://dev.epicgames.com/documentation/en-us/metahuman/metahumans-on-fab"), nullptr, nullptr);
					return FReply::Handled();
				})
				.Text(LOCTEXT("LearnMoreButton", "Learn more."))
			]
		]
	];
}
} // namespace UE::MetaHuman

#undef LOCTEXT_NAMESPACE
