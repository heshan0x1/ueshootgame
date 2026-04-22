// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementUObjectNameQueries.generated.h"

UCLASS()
class UObjectNameDataStorageFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UObjectNameDataStorageFactory() override = default;

	void RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;

private:

	/**
	 * Adds the Name columns to new actors that do not have one already.
	 */
	void RegisterUObjectAddNameColumn(UE::Editor::DataStorage::ICoreProvider& DataStorage) const;

};

