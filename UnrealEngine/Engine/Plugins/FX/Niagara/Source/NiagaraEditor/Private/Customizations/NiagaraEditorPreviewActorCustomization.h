// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

class FNiagaraEditorPreviewActorCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	static FName GetCustomizationTypeName();

	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;
};

