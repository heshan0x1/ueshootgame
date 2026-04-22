// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraActorPlacement.h"
#include "ActorFactoryNiagara.h"

#include "IPlacementModeModule.h"

namespace FNiagaraActorPlacement
{
	static TOptional<FPlacementModeID> PlacementModeID;

	void Register()
	{
		if (GIsEditor == false || PlacementModeID.IsSet() )
		{
			return;
		}

		if (IPlacementModeModule::IsAvailable())
		{
			PlacementModeID = IPlacementModeModule::Get().RegisterPlaceableItem(
				FBuiltInPlacementCategories::Visual(),
				MakeShared<FPlaceableItem>(*UActorFactoryNiagara::StaticClass())
			);
		}
	}

	void Unregister()
	{
		if (PlacementModeID.IsSet() == false)
		{
			return;
		}
		if (IPlacementModeModule::IsAvailable())
		{
			IPlacementModeModule::Get().UnregisterPlaceableItem(PlacementModeID.GetValue());
		}
		PlacementModeID.Reset();
	}
}
