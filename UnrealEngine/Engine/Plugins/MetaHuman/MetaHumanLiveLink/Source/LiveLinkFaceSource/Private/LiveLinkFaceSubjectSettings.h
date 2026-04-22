// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanLiveLinkSubjectSettings.h"

#include "LiveLinkFaceSubjectSettings.generated.h"

UCLASS(BlueprintType)
class ULiveLinkFaceSubjectSettings : public UMetaHumanLiveLinkSubjectSettings
{
public:

	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	bool bHeadOrientation = true;

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	void SetHeadOrientation(bool HeadOrientation);

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	void GetHeadOrientation(bool& HeadOrientation) const;

	UPROPERTY(EditAnywhere, Category = "Controls", meta = (EditCondition = "bIsLiveProcessing", HideEditConditionToggle, EditConditionHides))
	bool bHeadTranslation = true;

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	void SetHeadTranslation(bool HeadTranslation);

	UFUNCTION(BlueprintCallable, Category = "Live Link Face")
	void GetHeadTranslation(bool& HeadTranslation) const;
};
