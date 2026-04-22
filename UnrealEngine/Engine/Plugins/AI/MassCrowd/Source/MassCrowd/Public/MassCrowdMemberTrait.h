// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTraitBase.h"
#include "MassCrowdMemberTrait.generated.h"

#define UE_API MASSCROWD_API

/**
 * Trait to mark an entity with the crowd tag and add required fragments to track current lane
 */
UCLASS(MinimalAPI, meta = (DisplayName = "CrowdMember"))
class UMassCrowdMemberTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:
	UE_API virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};

#undef UE_API
