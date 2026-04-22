// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowInstance.h"
#include "UObject/ObjectPtr.h"

#include "DataflowAttachment.generated.h"

/**
 * Generic dataflow instance attachment implemented as a user asset data object so it can be embedded inside
 * any asset that is a user-data container. This allows assets in modules that do not, or cannot know about
 * dataflow (e.g. Engine module assets) to contain a dataflow attachment.
 * Using a modular feature implementation this data can be grabbed as an asset opens and a plugin specific
 * implementation can be used for the asset editor (see FDataflowPhysicsAssetEditorOverride)
 */
UCLASS()
class UDataflowAttachment : public UAssetUserData, public IDataflowContentOwner, public IDataflowInstanceInterface
{
	GENERATED_BODY()

public:

	// IDataflowContentOwner
	void WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const override;
	void ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	// IDataflowInstanceInterface
	const FDataflowInstance& GetDataflowInstance() const override;
	FDataflowInstance& GetDataflowInstance() override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

private:

	// Dataflow instance for the attachment when editing.
	UPROPERTY()
	FDataflowInstance Instance;

protected:
	
	// IDataflowContentOwner
	TObjectPtr<UDataflowBaseContent> CreateDataflowContent() override;
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////
};