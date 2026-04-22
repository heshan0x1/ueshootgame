// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class URCController;

class SCustomStatelessEventControllerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCustomStatelessEventControllerWidget)
		{}
	SLATE_END_ARGS()

	/**
	 * Constructs this widget with InArgs
	 * @param InOriginalPropertyHandle Original PropertyHandle of this CustomWidget
	 */
	void Construct(const FArguments& InArgs, URCController* InController, const TSharedPtr<IPropertyHandle>& InOriginalPropertyHandle);

private:
	/** The controller associated with this widget. */
	TWeakObjectPtr<URCController> ControllerWeak;

	FReply OnButtonClicked();
};
