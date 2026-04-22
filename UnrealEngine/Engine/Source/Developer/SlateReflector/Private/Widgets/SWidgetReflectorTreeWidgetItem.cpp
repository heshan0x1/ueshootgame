// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SWidgetReflectorTreeWidgetItem.h"
#include "SlateOptMacros.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SHyperlink.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "SWidgetReflector"

/* SMultiColumnTableRow overrides
 *****************************************************************************/

FName SReflectorTreeWidgetItem::NAME_WidgetName(TEXT("WidgetName"));
FName SReflectorTreeWidgetItem::NAME_WidgetInfo(TEXT("WidgetInfo"));
FName SReflectorTreeWidgetItem::NAME_Visibility(TEXT("Visibility"));
FName SReflectorTreeWidgetItem::NAME_Focusable(TEXT("Focusable"));
FName SReflectorTreeWidgetItem::NAME_Enabled(TEXT("Enabled"));
FName SReflectorTreeWidgetItem::NAME_Volatile(TEXT("Volatile"));
FName SReflectorTreeWidgetItem::NAME_HasActiveTimer(TEXT("HasActiveTimer"));
FName SReflectorTreeWidgetItem::NAME_Clipping(TEXT("Clipping"));
FName SReflectorTreeWidgetItem::NAME_LayerId(TEXT("LayerId"));
FName SReflectorTreeWidgetItem::NAME_ForegroundColor(TEXT("ForegroundColor"));
FName SReflectorTreeWidgetItem::NAME_ActualSize(TEXT("ActualSize"));
FName SReflectorTreeWidgetItem::NAME_Address(TEXT("Address"));

void SReflectorTreeWidgetItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	this->WidgetInfo = InArgs._WidgetInfoToVisualize;
	this->OnAccessSourceCode = InArgs._SourceCodeAccessor;
	this->OnAccessAsset = InArgs._AssetAccessor;
	this->SetPadding(0.f);

	check(WidgetInfo.IsValid());

	SMultiColumnTableRow< TSharedRef<FWidgetReflectorNodeBase> >::Construct(SMultiColumnTableRow< TSharedRef<FWidgetReflectorNodeBase> >::FArguments().Padding(0.f), InOwnerTableView);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
TSharedRef<SWidget> SReflectorTreeWidgetItem::GenerateWidgetForColumn(const FName& ColumnName)
{
	SReflectorTreeWidgetItem* Self = this;
	auto BuildCheckBox = [Self](bool bIsChecked)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.0f, 0.0f))
				[
					SNew(SCheckBox)
					.Style(FCoreStyle::Get(), TEXT("WidgetReflector.FocusableCheck"))
					.IsChecked(bIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				];
		};

	if (ColumnName == NAME_WidgetName )
	{
		TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

		HorizontalBox->AddSlot()
		.AutoWidth()
		[
			SNew(SExpanderArrow, SharedThis(this))
			.IndentAmount(16)
			.ShouldDrawWires(true)
		];

#if UE_SLATE_WITH_DYNAMIC_INVALIDATION
		// Here we show an indication for Dynamic Invalidation Panels and their children.
		// Dynamic Invalidation Panels are Invalidation Panels with UseDynamicInvalidation set to true.
		static FName InvalidationPanelName = TEXT("SInvalidationPanel");

		const TSharedPtr<SWidget> LiveWidget = WidgetInfo->GetLiveWidget();
		TSharedPtr<SWidget> InvalidationPanel = LiveWidget;
		while (InvalidationPanel && InvalidationPanel->GetType() != InvalidationPanelName)
		{
			InvalidationPanel = InvalidationPanel->GetParentWidget();
		}

		if (InvalidationPanel && StaticCastSharedPtr<SInvalidationPanel>(InvalidationPanel)->GetUseDynamicInvalidation()) // if we found a Dynamic Invalidation Panel
		{
			const bool CanCache = StaticCastSharedPtr<SInvalidationPanel>(InvalidationPanel)->GetCanCache();
			// we always show a color block for the Dynamic Invalidation Panel, but for its children we only show the color if it can cache
			const bool bShowCachingIndicator = CanCache || LiveWidget == InvalidationPanel; 
			if (bShowCachingIndicator)
			{
				TPair<FLinearColor, FText> ColorAndTooltip;
			
				if (LiveWidget == InvalidationPanel)
				{
					if (!CanCache)
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(1.0f, 0.0f, 0.0f), // Red for disabled Dynamic Invalidation Panel
							LOCTEXT("DynamicInvalidationPanel_Disabled", "Dynamic Invalidation Panel cannot cache")
						};
					}
					else if (LiveWidget->SupportsInvalidationRecursive())
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(0.0f, 0.5f, 1.0f), // Blue for Dynamic Invalidation Panel that recursively supports invalidation
							LOCTEXT("DynamicInvalidationPanel_SupportingInvalidationRecursive", "Dynamic Invalidation Panel is caching all its children")
						} ;
					}
					else
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(1.f, 0.f, 1.f), // Magenta for Dynamic Invalidation Panel that is not able to recursively supports invalidation
							LOCTEXT("DynamicInvalidationPanel_NotSupportingInvalidationRecursive", "Dynamic Invalidation Panel supports invalidation but not all its children do")
						};
					}
				}
				else
				{
					if (!LiveWidget->SupportsInvalidation())
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(1.0f, 0.0f, 0.0f), // Red for widgets not supporting invalidation
							LOCTEXT("DynamicInvalidationPanel_WidgetNotSupportingInvalidation", "Widget does not support invalidation")
						};
					}
					else if (LiveWidget->SupportsInvalidationRecursive())
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(0.0f, 1.0f, 0.0f), // Green for widgets recursively supporting invalidation
							LOCTEXT("DynamicInvalidationPanel_WidgetSupportingInvalidationRecursive", "Widget and children all support invalidation")
						};
					}
					else
					{
						ColorAndTooltip = TPair<FLinearColor, FText> {
							FLinearColor(1.0f, .5f, 0.0f), // Orange for widgets supporting invalidation but having children that do not
							LOCTEXT("DynamicInvalidationPanel_WidgetNotSupportingInvalidationRecursive", "Widget supports invalidation but not all its children do")
						};
					}
				}

				HorizontalBox->AddSlot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SColorBlock)
					.Size(FVector2D{10.0f, 10.0f})
					.Color(ColorAndTooltip.Key)
					.ToolTipText(ColorAndTooltip.Value)
				];
			}
		}
#endif

		if (WidgetInfo->GetWidgetIsInvalidationRoot())
		{
			HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidationRoot_Short", "[IR]"))
			];
		}

		HorizontalBox->AddSlot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(WidgetInfo->GetWidgetTypeAndShortName())
			.ColorAndOpacity(this, &SReflectorTreeWidgetItem::GetTint)
		];

		return HorizontalBox;
	}
	else if (ColumnName == NAME_WidgetInfo )
	{
		return SNew(SBox)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SHyperlink)
				.Text(WidgetInfo->GetWidgetReadableLocation())
				.OnNavigate(this, &SReflectorTreeWidgetItem::HandleHyperlinkNavigate)
			];
	}
	else if (ColumnName == NAME_Visibility )
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(WidgetInfo->GetWidgetVisibilityText())
					.Justification(ETextJustify::Center)
			];
	}
	else if (ColumnName == NAME_Focusable)
	{
		return BuildCheckBox(WidgetInfo->GetWidgetFocusable());
	}
	else if (ColumnName == NAME_Enabled)
	{
		return BuildCheckBox(WidgetInfo->GetWidgetEnabled());
	}
	else if (ColumnName == NAME_Volatile)
	{
		return BuildCheckBox(WidgetInfo->GetWidgetIsVolatile());
	}
	else if (ColumnName == NAME_HasActiveTimer)
	{
		return BuildCheckBox(WidgetInfo->GetWidgetHasActiveTimers());
	}
	else if ( ColumnName == NAME_Clipping )
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(WidgetInfo->GetWidgetClippingText())
			];
	}
	else if (ColumnName == NAME_LayerId)
	{
		return SNew(SBox)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("WidgetLayerIds", "[{0}, {1}]"), FText::AsNumber(WidgetInfo->GetWidgetLayerId()), FText::AsNumber(WidgetInfo->GetWidgetLayerIdOut())))
			];
	}
	else if (ColumnName == NAME_ForegroundColor )
	{
		const FSlateColor Foreground = WidgetInfo->GetWidgetForegroundColor();

		return SNew(SBorder)
			// Show unset color as an empty space.
			.Visibility(Foreground.IsColorSpecified() ? EVisibility::Visible : EVisibility::Hidden)
			// Show a checkerboard background so we can see alpha values well
			.BorderImage(FCoreStyle::Get().GetBrush("Checkerboard"))
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				// Show a color block
				SNew(SColorBlock)
					.Color(Foreground.GetSpecifiedColor())
					.Size(FVector2D(16.0f, 16.0f))
			];
	}
	else if (ColumnName == NAME_ActualSize)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(WidgetInfo->GetLocalSize().ToString()));
	}
	else if (ColumnName == NAME_Address )
	{
		const FString WidgetAddress = FWidgetReflectorNodeUtils::WidgetAddressToString(WidgetInfo->GetWidgetAddress());
		const FText Address = FText::FromString(WidgetAddress);
		const FString ConditionalBreakPoint = FString::Printf(TEXT("this == (SWidget*)%s"), *WidgetAddress);

		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SHyperlink)
				.ToolTipText(LOCTEXT("ClickToCopyBreakpoint", "Click to copy conditional breakpoint for this instance."))
				.Text(LOCTEXT("CBP", "[CBP]"))
				.OnNavigate_Lambda([ConditionalBreakPoint](){ FPlatformApplicationMisc::ClipboardCopy(*ConditionalBreakPoint); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
			[
				SNew(SHyperlink)
				.ToolTipText(LOCTEXT("ClickToCopy", "Click to copy address."))
				.Text(Address)
				.OnNavigate_Lambda([Address]() { FPlatformApplicationMisc::ClipboardCopy(*Address.ToString()); })
			];
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SReflectorTreeWidgetItem::HandleHyperlinkNavigate()
{
	FAssetData CachedAssetData = WidgetInfo->GetWidgetAssetData();
	if (CachedAssetData.IsValid())
	{
		if (OnAccessAsset.IsBound())
		{
			CachedAssetData.GetPackage();
			OnAccessAsset.Execute(CachedAssetData.GetAsset());
			return;
		}
	}

	if (OnAccessSourceCode.IsBound())
	{
		OnAccessSourceCode.Execute(WidgetInfo->GetWidgetFile(), WidgetInfo->GetWidgetLineNumber(), 0);
	}
}

#undef LOCTEXT_NAMESPACE