// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ITraitInterface.h"

namespace UE::UAF
{
	/**
	 * IScopedTraitInterface
	 * 
	 * Base type for all scoped trait interfaces. Used for type safety.
	 */
	struct IScopedTraitInterface : ITraitInterface
	{
		DECLARE_ANIM_TRAIT_INTERFACE(IScopedTraitInterface)
	};
}
