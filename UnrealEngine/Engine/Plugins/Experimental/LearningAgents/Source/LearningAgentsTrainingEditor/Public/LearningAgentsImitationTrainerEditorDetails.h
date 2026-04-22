// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"

#define UE_API LEARNINGAGENTSTRAININGEDITOR_API

/** Details customization for the imitation learning trainer */
class FLearningAgentsImitationTrainerEditorDetails : public IDetailCustomization
{
public:
	UE_API static TSharedRef<IDetailCustomization> MakeInstance();
	UE_API virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TArray<TWeakObjectPtr<UObject>> EditedObjects;
	FReply OnRunClicked();
	FReply OnStopClicked();
};

#undef UE_API
