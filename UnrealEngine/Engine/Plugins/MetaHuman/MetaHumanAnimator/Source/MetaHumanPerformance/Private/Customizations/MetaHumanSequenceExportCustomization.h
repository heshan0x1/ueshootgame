// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "IDetailCustomization.h"

class FMetaHumanSequenceExportCustomization
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~End IDetailCustomization interface

private:
	bool ShouldFilterBlueprint(const FAssetData& InAssetData) const;
};