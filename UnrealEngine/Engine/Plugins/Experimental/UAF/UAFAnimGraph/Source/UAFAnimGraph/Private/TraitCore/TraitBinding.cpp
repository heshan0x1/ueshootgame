// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraitCore/TraitBinding.h"

#include "TraitCore/ITraitInterface.h"

namespace UE::UAF
{
	FTraitInterfaceUID FTraitBinding::GetInterfaceUID() const
	{
		return InterfaceThisOffset != -1 ? GetInterfaceTyped<ITraitInterface>()->GetInterfaceUID() : FTraitInterfaceUID();
	}
}
