// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BakingAnimationKeySettings.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StructOnScope.h"

class ISequencer;
class IStructureDetailsView;
class SWindow;

DECLARE_DELEGATE_RetVal_OneParam(FReply, SBakeTransformOnBake, FBakingAnimationKeySettings);

/** Widget allowing baking controls from one space to another */
class SBakeTransformWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SBakeTransformWidget)
		: _Sequencer(nullptr)
		{}
		SLATE_ARGUMENT(ISequencer*, Sequencer)
		SLATE_ARGUMENT(FBakingAnimationKeySettings, Settings)
		SLATE_EVENT(SBakeTransformOnBake, OnBake)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBakeTransformWidget() override {}

	FReply OpenDialog(bool bModal = true);
	void CloseDialog();

private:

	//used for setting up the details
	TSharedPtr<TStructOnScope<FBakingAnimationKeySettings>> Settings;

	ISequencer* Sequencer;

	TWeakPtr<SWindow> DialogWindow;
	TSharedPtr<IStructureDetailsView> DetailsView;
};
