// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanSkeletalClothing.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * Verifies that a piece of clothing conforms to the standard for skeletal mesh-based clothing packages
 */
UCLASS(MinimalAPI)
class UVerifyMetaHumanSkeletalClothing : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;

	static UE_API void VerifyClothingCompatibleAssets(const UObject* ToVerify, UMetaHumanAssetReport* Report);
};

#undef UE_API
