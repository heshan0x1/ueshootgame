// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyObjectValid.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * A simple rule to test if a UObject is a valid asset
 */
UCLASS(MinimalAPI, BlueprintType)
class UVerifyObjectValid : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	// UMetaHumanVerificationRuleBase overrides
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;
};

#undef UE_API
