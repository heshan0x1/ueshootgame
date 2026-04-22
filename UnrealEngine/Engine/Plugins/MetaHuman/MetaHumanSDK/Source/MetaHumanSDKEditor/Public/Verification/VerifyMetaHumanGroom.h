// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanGroom.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * A rule to test if a UObject complies with the MetaHuman Groom standard
 */
UCLASS(MinimalAPI, BlueprintType)
class UVerifyMetaHumanGroom : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	// UMetaHumanVerificationRuleBase overrides
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;
};

#undef UE_API
