// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanOutfitClothing.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * Verifies that a piece of clothing conforms to the standard for outfit-based clothing packages
 */
UCLASS(MinimalAPI)
class UVerifyMetaHumanOutfitClothing : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;
};

#undef UE_API
