// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetaHumanVerificationRuleCollection.h"

#include "VerifyMetaHumanCharacter.generated.h"

#define UE_API METAHUMANSDKEDITOR_API

/**
 * A verification rule that tests that a MetaHuman character is valid. Currently only handles "Legacy" MetaHuman Characters.
 */
UCLASS(MinimalAPI)
class UVerifyMetaHumanCharacter : public UMetaHumanVerificationRuleBase
{
	GENERATED_BODY()

public:
	// UMetaHumanVerificationRuleBase overrides
	UE_API virtual void Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const override;
};

#undef UE_API
