// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facades/PVRenderingFacade.h"

namespace PV::Facades
{
	FPVRenderingFacade::FPVRenderingFacade(FManagedArrayCollection& InSelf)
		: FRenderingFacade(InSelf)
		,ConstProceduralVegetationCollection(InSelf)
		,ProceduralVegetationCollection(&InSelf)
		,FoliageFacade(InSelf)
	{}

	FPVRenderingFacade::FPVRenderingFacade(const FManagedArrayCollection& InSelf)
		: FRenderingFacade(InSelf)
		,ConstProceduralVegetationCollection(InSelf)
		,ProceduralVegetationCollection(nullptr)
		,FoliageFacade(InSelf)
	{}

	void FPVRenderingFacade::DefineSchema()
	{
		FRenderingFacade::DefineSchema();
	}

	const FFoliageFacade& FPVRenderingFacade::GetFoliageFacade() const
	{
		return FoliageFacade;
	}

	void FPVRenderingFacade::CopyFrom(const FManagedArrayCollection& Other)
	{
		Other.CopyTo(ProceduralVegetationCollection);
	}
}

