// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FUICommandInfo;
class FUICommandList;
class SNotificationItem;
class SWidget;
class FKPIValue;
struct FSlateBrush;


enum class EEditorPerformanceState : uint8
{
	Good,
	Warnings,
};

class SEditorPerformanceStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SEditorPerformanceStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	static FReply ViewPerformanceReport_Clicked();

	const FSlateBrush*				GetStatusIcon() const;
	const FSlateBrush*				GetStatusBadgeIcon() const;
	FText							GetStatusToolTipText() const;
	void							UpdateState();
	
	TSharedPtr<SNotificationItem>	NotificationItem;
	EEditorPerformanceState			EditorPerformanceState= EEditorPerformanceState::Good;
	FText							EditorPerformanceStateMessage;
	FText							CurrentNotificationMessage;
	FName							CurrentNotificationName;
	TArray<FName>					AcknowledgedNotifications;
	uint32							WarningCount = 0;

};

