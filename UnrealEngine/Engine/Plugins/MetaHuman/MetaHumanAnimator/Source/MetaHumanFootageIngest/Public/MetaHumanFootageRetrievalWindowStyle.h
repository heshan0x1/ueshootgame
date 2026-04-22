// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** A Style set for Footage Retrieval window */
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanFootageIngest is deprecated. This functionality is now available in the CaptureManager module")
	FMetaHumanFootageRetrievalWindowStyle : public FSlateStyleSet
{
public:

	virtual const FName& GetStyleSetName() const override;
	static const FMetaHumanFootageRetrievalWindowStyle& Get();

	static void ReloadTextures();

	static void Register();
	static void Unregister();

private:

	FMetaHumanFootageRetrievalWindowStyle();

	static FName StyleName;
};