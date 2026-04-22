// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IDetailCustomization.h"

#define UE_API CAPTUREDATAEDITOR_API

class FFootageCaptureDataCustomization
	: public IDetailCustomization
{
public:
	static UE_API TSharedRef<IDetailCustomization> MakeInstance();

	//~Begin IDetailCustomization interface
	UE_API virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~End IDetailCustomization interface
};

#undef UE_API
