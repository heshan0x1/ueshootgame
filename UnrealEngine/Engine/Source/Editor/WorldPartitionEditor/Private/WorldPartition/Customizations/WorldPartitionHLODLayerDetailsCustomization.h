// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UHLODLayer;

class FWorldPartitionHLODLayerDetailsCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	~FWorldPartitionHLODLayerDetailsCustomization();

private:
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayoutBuilder) override;
	void OnWorldPartitionChanged(UWorld* InWorld);

	IDetailLayoutBuilder* DetailLayoutBuilder;
};