// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanPackageSource.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * A generic rule for MetaHuman Asset Groups that tests that they are valid for the generation of a MetaHuman Package.
 * Only works for "normal" Asset Groups like grooms and clothing, not legacy characters.
 */
UCLASS(MinimalAPI)
class UVerifyMetaHumanPackageSource : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	// UMetaHumanVerificationRuleBase overrides
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;
};

#undef UE_API
