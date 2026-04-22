// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IMetaHumanValidationContext.generated.h"

UINTERFACE()
class UMetaHumanValidationContext : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief Interface for a validation context to be used by pipelines to validate items
 */
class IMetaHumanValidationContext
{
	GENERATED_BODY()

public:

	/**
	 * @brief Validates a wardrobe item using the rules defined in the MetaHuman SDK
	 * 
	 * @param InWardrobeItem The item to be validated
	 * @return true if the item is valid and false otherwise
	 */
	virtual bool ValidateWardrobeItem(const class UMetaHumanWardrobeItem* InWardrobeItem) = 0;
};