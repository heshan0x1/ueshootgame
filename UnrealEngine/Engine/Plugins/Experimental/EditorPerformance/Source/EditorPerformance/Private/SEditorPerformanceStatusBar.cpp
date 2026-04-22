// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEditorPerformanceStatusBar.h"

#include "Async/Future.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"
#include "EditorPerformanceModule.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/SlateDelegates.h"
#include "HAL/PlatformCrt.h"
#include "ISettingsModule.h"
#include "Internationalization/Internationalization.h"
#include "Layout/Children.h"
#include "Layout/Margin.h"
#include "Math/Color.h"
#include "Math/UnitConversion.h"
#include "Math/UnrealMathSSE.h"
#include "Misc/Attribute.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"
#include "SlotBase.h"
#include "Styling/AppStyle.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Textures/SlateIcon.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Settings/EditorProjectSettings.h"
#include "Editor/EditorPerformanceSettings.h"

class SWidget;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "EditorPerformance"


FReply SEditorPerformanceStatusBarWidget::ViewPerformanceReport_Clicked()
{
	FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance").ShowPerformanceReportTab();

	return FReply::Handled();
}

void SEditorPerformanceStatusBarWidget::Construct(const FArguments& InArgs)
{
	this->ChildSlot
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("StatusBar.StatusBarButton"))
			.Content()
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.Padding(6.f, 0.f)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 3.f, 0.f)
					[
						SNew(SOverlay)
						+SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image_Lambda([this] { return GetStatusIcon(); })
							.ToolTipText_Lambda([this] { return GetStatusToolTipText(); })
						]
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image_Lambda([this] { return GetStatusBadgeIcon(); })
						]
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Diagnostics", "Diagnostics"))
					]
				]
			]
			.OnClicked(FOnClicked::CreateStatic(&SEditorPerformanceStatusBarWidget::ViewPerformanceReport_Clicked))
		];

	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");
	EditorPerfModule.GetOnPerformanceStateChanged().AddSP(this, &SEditorPerformanceStatusBarWidget::UpdateState);
}

void SEditorPerformanceStatusBarWidget::UpdateState()
{
	EditorPerformanceState = EEditorPerformanceState::Good;
	EditorPerformanceStateMessage = LOCTEXT("ToolTipMessageGood", "Nothing to report.");

	FEditorPerformanceModule& EditorPerfModule = FModuleManager::LoadModuleChecked<FEditorPerformanceModule>("EditorPerformance");

	const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>();

	WarningCount = 0;

	FText NotificationTitle;

	// Check for KPIs that have exceeded their value
	for (FKPIValues::TConstIterator It(EditorPerfModule.GetKPIRegistry().GetKPIValues()); It; ++It)
	{
		const FKPIValue& KPIValue = It->Value;

		if (KPIValue.GetState() == FKPIValue::Bad)
		{
			if (EditorPerformanceSettings)
			{
				if (EditorPerformanceSettings->NotifyList.Find(KPIValue.Path) != INDEX_NONE)
				{
					// Turn into a warning state if the user wants to be notified	
					EditorPerformanceState = EEditorPerformanceState::Warnings;

					if (AcknowledgedNotifications.Find(KPIValue.Path) == INDEX_NONE && CurrentNotificationName.IsNone())
					{
						NotificationTitle = KPIValue.DisplayName;

						FKPIHint Hint;
						if (EditorPerfModule.GetKPIRegistry().GetKPIHint(KPIValue.Id, Hint))
						{
							CurrentNotificationMessage = Hint.Message;
						}
						else
						{
							CurrentNotificationMessage = FText::FromString(*FString::Printf(TEXT("%s - %s was %s but should be %s %s"),
								*KPIValue.Category.ToString(),
								*KPIValue.Name.ToString(),
								*FKPIValue::GetValueAsString(KPIValue.CurrentValue, KPIValue.DisplayType, KPIValue.CustomDisplayValueGetter),
								*FKPIValue::GetComparisonAsPrettyString(KPIValue.Compare),
								*FKPIValue::GetValueAsString(KPIValue.ThresholdValue.GetValue(), KPIValue.DisplayType, KPIValue.CustomDisplayValueGetter)));
						}

						CurrentNotificationName = KPIValue.Path;
					}
				}
			}

			WarningCount++;
		}
		else
		{
			// No longer exceeding threshold, so no need to acknowledge the last time it was raised to the user
			// There may be subsequent times that this same KPI is exceeded this session so we may want to alert the user again
			AcknowledgedNotifications.Remove(KPIValue.Path);

			if (CurrentNotificationName == KPIValue.Path)
			{
				CurrentNotificationName = FName();
			}
		}
	}

	if (WarningCount > 0)
	{
		EditorPerformanceStateMessage = LOCTEXT("ToolTipWarning", "Warning. View report for details.");
	}

	if (CurrentNotificationName.IsNone() == false && EditorPerformanceSettings && EditorPerformanceSettings->bEnableNotifications)
	{	
		if (NotificationItem.IsValid() == false || NotificationItem->GetCompletionState() == SNotificationItem::CS_None)
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("NotificationTitle", "{0} Warning"), NotificationTitle));

			Info.SubText = CurrentNotificationMessage;
			Info.bUseSuccessFailIcons = true;
			Info.bFireAndForget = false;
			Info.bUseThrobber = true;
			Info.FadeOutDuration = 1.0f;
			Info.ExpireDuration = 0.0f;

			// No existing notification or the existing one has finished
			TPromise<TWeakPtr<SNotificationItem>> AcknowledgeNotificationPromise;

			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("AcknowledgeNotificationButton", "Dismiss"), FText(), FSimpleDelegate::CreateLambda([NotificationFuture = AcknowledgeNotificationPromise.GetFuture().Share(),this]()
				{
					// User has acknowledged this warning
					TWeakPtr<SNotificationItem> NotificationPtr = NotificationFuture.Get();
					if (TSharedPtr<SNotificationItem> Notification = NotificationPtr.Pin())
					{
						Notification->SetCompletionState(SNotificationItem::CS_None);
						Notification->ExpireAndFadeout();
					}

					AcknowledgedNotifications.Add(CurrentNotificationName);
					CurrentNotificationName = FName();

				}), SNotificationItem::ECompletionState::CS_Fail));

			// Add a "Don't show this again" option
			Info.CheckBoxState = TAttribute<ECheckBoxState>::CreateLambda([CurrentNotificationName=this->CurrentNotificationName]()
				{
					if (CurrentNotificationName.IsNone() == false)
					{
						if (const UEditorPerformanceSettings* EditorPerformanceSettings = GetDefault<UEditorPerformanceSettings>())
						{
							return EditorPerformanceSettings->NotifyList.Contains(CurrentNotificationName) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
						}
					}

					return ECheckBoxState::Unchecked;
				});
			Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateLambda([CurrentNotificationName=this->CurrentNotificationName](ECheckBoxState NewState)
				{
					if (CurrentNotificationName.IsNone())
					{
						return;
					}

					if (UEditorPerformanceSettings* EditorPerformanceSettings = GetMutableDefault<UEditorPerformanceSettings>())
					{
						switch (NewState)
						{
							case ECheckBoxState::Checked:
								EditorPerformanceSettings->NotifyList.Remove(CurrentNotificationName);
								break;
							case ECheckBoxState::Unchecked:
								EditorPerformanceSettings->NotifyList.AddUnique(CurrentNotificationName);
								break;
							default:
								break;
						}

						EditorPerformanceSettings->PostEditChange();
						EditorPerformanceSettings->SaveConfig();
					}
				});
			Info.CheckBoxText = LOCTEXT("DontShowThisAgainCheckBoxMessage", "Don't show this again");


			// Create the notification item
			NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

			if (NotificationItem.IsValid())
			{
				AcknowledgeNotificationPromise.SetValue(NotificationItem);
				NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
			}
		}
	}
	// No longer any warnings so kill any existing notifications
	else if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_None);
		NotificationItem->ExpireAndFadeout();
	}
}

const FSlateBrush* SEditorPerformanceStatusBarWidget::GetStatusIcon() const
{
	switch (EditorPerformanceState)
	{
		default:
		case EEditorPerformanceState::Good:
		{
			return FAppStyle::Get().GetBrush("EditorDiagnostics.StatusBar.Icon");
		}

		case EEditorPerformanceState::Warnings:
		{
			return FAppStyle::Get().GetBrush("EditorDiagnostics.StatusBar.BadgeBG");
		}
	}
}

const FSlateBrush* SEditorPerformanceStatusBarWidget::GetStatusBadgeIcon() const
{
	switch (EditorPerformanceState)
	{
		default:
		case EEditorPerformanceState::Good:
		{
			return FAppStyle::Get().GetBrush("NoBrush");
		}

		case EEditorPerformanceState::Warnings:
		{
			return FAppStyle::Get().GetBrush("EditorDiagnostics.StatusBar.WarningBadge");
		}
	}
}

FText SEditorPerformanceStatusBarWidget::GetStatusToolTipText() const
{
	return FText::Format(LOCTEXT("ToolTipFormat", "Editor Diagnostics: {0}"), EditorPerformanceStateMessage);
}

#undef LOCTEXT_NAMESPACE
