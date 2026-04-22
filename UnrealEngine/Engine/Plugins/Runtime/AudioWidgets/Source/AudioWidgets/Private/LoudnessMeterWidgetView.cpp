// Copyright Epic Games, Inc. All Rights Reserved.

#include "LoudnessMeterWidgetView.h"

#include "AudioWidgetsStyle.h"
#include "AudioMeterWidgetStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SAudioMeterWidget.h"
#include "Styling/StyleColors.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Views/STileView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "FLoudnessMeterWidgetView"

namespace AudioWidgets
{
	namespace LoudnessMeterWidgetView_Private
	{
		FText FormatNumericValueText(TOptional<float> Value)
		{
			if (!Value.IsSet())
			{
				return FText::GetEmpty();
			}
			else if (*Value == -std::numeric_limits<float>::infinity())
			{
				return LOCTEXT("NegativeInfinity", "-∞");
			}

			static const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
				.SetMinimumFractionalDigits(1)
				.SetMaximumFractionalDigits(1);

			return FText::AsNumber(*Value, &NumberFormattingOptions);
		}

		TSharedRef<ITableRow> MakeNumericValueTileWidget(TSharedRef<const FLoudnessMeterWidgetView::FLoudnessMetric> InItem, const TSharedRef<STableViewBase>& OwnerTable)
		{
			using FLoudnessMetricRef = TSharedRef<const FLoudnessMeterWidgetView::FLoudnessMetric>;

			return SNew(STableRow<FLoudnessMetricRef>, OwnerTable)
				[
					SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.MinHeight(8)
						.MaxHeight(8)
						[
							SNew(SSpacer)
						]
						+ SVerticalBox::Slot()
						.MinHeight(20)
						.MaxHeight(20)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						[
							SNew(STextBlock)
								.Text_Lambda([InItem]() { return FormatNumericValueText(InItem->Value.Get()); })
								.Font(FStyleDefaults::GetFontInfo(14))
								.ColorAndOpacity(FStyleColors::AccentGreen)
								.SimpleTextMode(true)
						]
						+ SVerticalBox::Slot()
						.MinHeight(32)
						.MaxHeight(32)
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock)
								.Text_Lambda([InItem]() { return InItem->DisplayName; })
								.Font(FStyleDefaults::GetFontInfo(8))
								.AutoWrapText(true)
								.Justification(ETextJustify::Center)
						]
				];
		}

		TSharedRef<ITableRow> MakeMeterValueTileWidget(TSharedRef<const FLoudnessMeterWidgetView::FLoudnessMetric> InItem, const TSharedRef<STableViewBase>& OwnerTable)
		{
			using FLoudnessMetricRef = TSharedRef<const FLoudnessMeterWidgetView::FLoudnessMetric>;

			const auto MakeAudioMeterWidgetStyle = []()
				{
					FAudioMeterWidgetStyle MeterStyle = FAudioMeterWidgetStyle::GetDefault();
					MeterStyle.SetMeterSize({ 700.0, 25.0 }); // MaxLength, Width.
					MeterStyle.SetValueRangeDb({ -60, 0 });
					MeterStyle.SetScaleHashHeight(4.0f); // This is the tick mark length.
					return MeterStyle;
				};

			static const FAudioMeterWidgetStyle AudioMeterWidgetStyle = MakeAudioMeterWidgetStyle();

			static const FSlateRoundedBoxBrush RoundedPanelBrush(FStyleColors::Panel, 4.0f);

			const FAudioMeterDefaultColorWidgetStyle& DefaultColorStyle = FAudioMeterDefaultColorWidgetStyle::GetDefault();

			return SNew(STableRow<FLoudnessMetricRef>, OwnerTable)
				[
					SNew(SBox)
						.VAlign(EVerticalAlignment::VAlign_Center)
						.Padding(4.0f)
						[
							SNew(SBorder)
								.BorderImage(&RoundedPanelBrush)
								.Padding(0.0f, 8.0f)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(EHorizontalAlignment::HAlign_Center)
										[
											SNew(STextBlock)
												.Text_Lambda([InItem]() { return FormatNumericValueText(InItem->Value.Get()); })
												.Font(FStyleDefaults::GetFontInfo(14))
												.ColorAndOpacity(FStyleColors::AccentGreen)
												.SimpleTextMode(true)
										]
										+ SVerticalBox::Slot()
										.HAlign(EHorizontalAlignment::HAlign_Center)
										[
											SNew(SAudioMeterWidget)
												.Orientation(EOrientation::Orient_Vertical)
												.BackgroundColor(FLinearColor::Transparent)
												.MeterBackgroundColor(DefaultColorStyle.MeterBackgroundColor)
												.MeterValueColor(DefaultColorStyle.MeterValueColor)
												.MeterPeakColor(FLinearColor::Transparent)
												.MeterClippingColor(FLinearColor::Transparent)
												.MeterScaleColor(FLinearColor::White.CopyWithNewOpacity(0.25f))
												.MeterScaleLabelColor(FLinearColor::White.CopyWithNewOpacity(0.25f))
												.Style(&AudioMeterWidgetStyle)
												.MeterChannelInfo_Lambda([InItem]() -> TArray<FAudioMeterChannelInfo>
													{
														constexpr float DefaultInvalidValue = -160.0f;
														const TOptional<float> LoudnessMetricValue = InItem->Value.Get();
														return { { LoudnessMetricValue.Get(DefaultInvalidValue), DefaultInvalidValue, DefaultInvalidValue } };
													})
										]
										+ SVerticalBox::Slot()
										.AutoHeight()
										.MinHeight(28)
										.HAlign(EHorizontalAlignment::HAlign_Center)
										.VAlign(EVerticalAlignment::VAlign_Center)
										[
											SNew(STextBlock)
												.Text_Lambda([InItem]() { return InItem->DisplayName; })
												.Font(FStyleDefaults::GetFontInfo(8))
												.AutoWrapText(true)
												.Justification(ETextJustify::Center)
										]
								]
						]
				];
		}
	} // namespace LoudnessMeterWidgetView_Private

	FLoudnessMeterWidgetView::FLoudnessMeterWidgetView()
		: LoudnessMetrics(MakeShared<FLoudnessMetricRefArray>())
		, NumericValues(MakeShared<FLoudnessMetricRefArray>())
		, MeterValues(MakeShared<FLoudnessMetricRefArray>())
	{
	}

	FLoudnessMeterWidgetView::~FLoudnessMeterWidgetView()
	{
		LoudnessMetrics->Reset();
		NumericValues->Reset();
		MeterValues->Reset();
	}

	void FLoudnessMeterWidgetView::InitTimerPanel(const FTimerPanelParams& InTimerPanelParams)
	{
		TimerPanelParams = InTimerPanelParams;
	}

	TSharedRef<SWidget> FLoudnessMeterWidgetView::MakeWidget() const
	{
		using SLoudnessMetricTileView = STileView<FLoudnessMetricRef>;

		static FSlateColorBrush BackgroundBrush(FStyleColors::Recessed);

		constexpr bool bForceSmallIcons = true;
		const TSharedRef<FUICommandList> CommandList = MakeShared<FUICommandList>();
		FSlimHorizontalToolBarBuilder ToolBarBuilder(CommandList, FMultiBoxCustomization::None, nullptr, bForceSmallIcons);

		const auto MakeOptionsMenu = [CommandList, TimerPanelParams = this->TimerPanelParams, LoudnessMetrics = this->LoudnessMetrics]() -> TSharedRef<SWidget>
			{
				constexpr bool bShouldCloseWindowAfterMenuSelection = true;
				FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

				{
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("LayoutOptions", "Layout Options"));

					if (TimerPanelParams.OnVisibilityToggleRequested.IsBound())
					{
						MenuBuilder.AddMenuEntry(
							LOCTEXT("LayoutOptions_Time_Label", "Time"),
							LOCTEXT("LayoutOptions_Time_ToolTip", "Display analysis timer"),
							FSlateIcon(),
							FUIAction(
								TimerPanelParams.OnVisibilityToggleRequested,
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([bIsVisible = TimerPanelParams.bIsVisible]() { return bIsVisible.Get(); })
							),
							NAME_None,
							EUserInterfaceActionType::ToggleButton);
					}

					for (const FLoudnessMetricRef& LoudnessMetricRef : LoudnessMetrics.Get())
					{
						MenuBuilder.AddSubMenu(
							LoudnessMetricRef->DisplayName,
							FText(),
							FNewMenuDelegate::CreateSPLambda(LoudnessMetricRef, [&LoudnessMetric = LoudnessMetricRef.Get()](FMenuBuilder& SubMenu)
								{
									SubMenu.BeginSection(NAME_None, LoudnessMetric.DisplayName);

									SubMenu.AddMenuEntry(
										LOCTEXT("LayoutOptions_LoudnessMetric_ShowValue", "Show Value"),
										FText(),
										FSlateIcon(),
										FUIAction(
											LoudnessMetric.OnShowValueToggleRequested,
											FCanExecuteAction::CreateLambda([bCanExecute = LoudnessMetric.OnShowValueToggleRequested.IsBound()]() { return bCanExecute; }),
											FIsActionChecked::CreateLambda([bShowValue = LoudnessMetric.bShowValue]() { return bShowValue.Get(); })),
										NAME_None,
										EUserInterfaceActionType::ToggleButton);

									SubMenu.AddMenuEntry(
										LOCTEXT("LayoutOptions_LoudnessMetric_ShowMeter", "Show Meter"),
										FText(),
										FSlateIcon(),
										FUIAction(
											LoudnessMetric.OnShowMeterToggleRequested,
											FCanExecuteAction::CreateLambda([bCanExecute = LoudnessMetric.OnShowMeterToggleRequested.IsBound()]() { return bCanExecute; }),
											FIsActionChecked::CreateLambda([bShowMeter = LoudnessMetric.bShowMeter]() { return bShowMeter.Get(); })),
										NAME_None,
										EUserInterfaceActionType::ToggleButton);

									SubMenu.EndSection();
								}));
					}

					MenuBuilder.EndSection();
				}

				return MenuBuilder.MakeWidget();
			};

		ToolBarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateLambda(MakeOptionsMenu),
			LOCTEXT("OptionsButtonDisplayName", "Options"),
			FText(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));

		const auto GetTimerPanelVisibility = [bIsVisible = TimerPanelParams.bIsVisible]() { return (bIsVisible.Get()) ? EVisibility::Visible : EVisibility::Collapsed; };
		const TAttribute<EVisibility> GetNumericValuesPanelVisibility = TAttribute<EVisibility>::CreateSPLambda(NumericValues, [&InNumericValues = NumericValues.Get()]()
			{
				return (!InNumericValues.IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
			});

		constexpr float MeterWidth = 79.0f;
		constexpr float MeterMinHeight = 160.0f;

		TSharedRef<SLoudnessMetricTileView> MeterTileView = SNew(SLoudnessMetricTileView)
			.OnGenerateTile_Static(&LoudnessMeterWidgetView_Private::MakeMeterValueTileWidget)
			.ListItemsSource(MeterValues)
			.ItemWidth(MeterWidth)
			.ItemAlignment(EListItemAlignment::CenterAligned)
			.SelectionMode(ESelectionMode::None)
			.ScrollbarVisibility(EVisibility::Collapsed);

		MeterTileView->SetItemHeight(TAttribute<float>::CreateLambda([&TileView = *MeterTileView]()
			{
				const int32 NumItems = TileView.GetItems().Num();
				const FVector2f PanelSize = TileView.GetTickSpaceGeometry().GetLocalSize();
				const int32 NumItemsPerLine = FMath::Max(1, FMath::FloorToInt(PanelSize.X / MeterWidth));
				const int32 NumLines = (NumItems + NumItemsPerLine - 1) / NumItemsPerLine;
				return FMath::Max(PanelSize.Y / NumLines, MeterMinHeight);
			}));

		return SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(&BackgroundBrush)
			[
				SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SNew(SColorBlock)
									.Color(FStyleColors::Panel.GetSpecifiedColor())
							]
							+ SOverlay::Slot()
							.HAlign(EHorizontalAlignment::HAlign_Right)
							[
								ToolBarBuilder.MakeWidget()
							]
					]
					+ SVerticalBox::Slot()
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.AutoHeight()
					[
						SNew(SBox)
							.Padding(0.0f, 10.0f)
							.Visibility_Lambda(GetTimerPanelVisibility)
							[
								SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									[
										SNew(SBorder)
											.Padding(8.0f, 4.0f)
											[
												SNew(STextBlock)
													.Font(FStyleDefaults::GetFontInfo(10))
													.Text_Lambda([AnalysisTime = TimerPanelParams.AnalysisTime]() { return FText::AsTimespan(AnalysisTime.Get()); })
											]
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									[
										SNew(SButton)
											.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
											.OnClicked(TimerPanelParams.OnResetButtonClicked)
											[
												SNew(SImage)
													.Image(FAudioWidgetsStyle::Get().GetBrush("AudioWidgetsStyle.Reset"))
													.ColorAndOpacity(FSlateColor::UseForeground())
													.ToolTipText(LOCTEXT("ResetButtonToolTip", "Reset Long Term loudness analysis"))
											]
									]
							]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.0f, 2.0f)
					[
						SNew(SSeparator)
							.Visibility_Lambda(GetTimerPanelVisibility)
							.SeparatorImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.Orientation(Orient_Horizontal)
							.Thickness(1.0f)
							.ColorAndOpacity(FLinearColor::White.CopyWithNewOpacity(0.25f))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SLoudnessMetricTileView)
							.Visibility(GetNumericValuesPanelVisibility)
							.OnGenerateTile_Static(&LoudnessMeterWidgetView_Private::MakeNumericValueTileWidget)
							.ListItemsSource(NumericValues)
							.ItemHeight(60.0f)
							.ItemWidth(60.0f)
							.ItemAlignment(EListItemAlignment::CenterAligned)
							.SelectionMode(ESelectionMode::None)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(8.0f, 2.0f)
					[
						SNew(SSeparator)
							.Visibility(GetNumericValuesPanelVisibility)
							.SeparatorImage(FAppStyle::Get().GetBrush("WhiteBrush"))
							.Orientation(Orient_Horizontal)
							.Thickness(1.0f)
							.ColorAndOpacity(FLinearColor::White.CopyWithNewOpacity(0.25f))
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						MeterTileView
					]
			];
	}

	void FLoudnessMeterWidgetView::AddLoudnessMetric(const FLoudnessMetric& LoudnessMetric)
	{
		FLoudnessMetricRef LoudnessMetricRef = MakeShared<FLoudnessMetric>(LoudnessMetric);
		LoudnessMetrics->Add(LoudnessMetricRef);

		if (LoudnessMetric.bShowValue.Get())
		{
			NumericValues->Add(LoudnessMetricRef);
		}

		if (LoudnessMetric.bShowMeter.Get())
		{
			MeterValues->Add(LoudnessMetricRef);
		}
	}

	void FLoudnessMeterWidgetView::RefreshVisibleLoudnessMetrics()
	{
		for (const FLoudnessMetricRef& LoudnessMetricRef : LoudnessMetrics.Get())
		{
			const bool bShouldShowValue = LoudnessMetricRef->bShowValue.Get();
			const bool bIsShowingValue = NumericValues->Contains(LoudnessMetricRef);
			if (bIsShowingValue != bShouldShowValue)
			{
				if (bShouldShowValue)
				{
					NumericValues->Add(LoudnessMetricRef);
				}
				else
				{
					NumericValues->RemoveSingle(LoudnessMetricRef);
				}
			}

			const bool bShouldShowMeter = LoudnessMetricRef->bShowMeter.Get();
			const bool bIsShowingMeter = MeterValues->Contains(LoudnessMetricRef);
			if (bIsShowingMeter != bShouldShowMeter)
			{
				if (bShouldShowMeter)
				{
					MeterValues->Add(LoudnessMetricRef);
				}
				else
				{
					MeterValues->RemoveSingle(LoudnessMetricRef);
				}
			}
		}
	}
} // namespace AudioWidgets

#undef LOCTEXT_NAMESPACE
