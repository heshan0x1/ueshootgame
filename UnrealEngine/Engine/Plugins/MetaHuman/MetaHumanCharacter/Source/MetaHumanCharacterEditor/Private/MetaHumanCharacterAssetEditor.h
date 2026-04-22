// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "MetaHumanCharacterAssetEditor.generated.h"

UCLASS()
class UMetaHumanCharacterAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	UMetaHumanCharacterAssetEditor();
	//~Begin UAssetEditor Interface
	virtual void GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;
	//~End UAssetEditor Interface

	class UMetaHumanCharacter* GetObjectToEdit() const;
	void SetObjectToEdit(class UMetaHumanCharacter* InObject);

	// returns unique editor session ID for analytics tracking
	// the GUID is created each time an asset editor is opened
	FGuid GetSessionGuid()
	{
		return EditorSessionGuid;
	}

private:

	UPROPERTY()
	TObjectPtr<class UMetaHumanCharacter> ObjectToEdit;

	FGuid EditorSessionGuid;
};