// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayAccessor.h"

namespace PV::Facades
{
	class PROCEDURALVEGETATION_API FMetaInfoFacade
	{
	public:
		FMetaInfoFacade(FManagedArrayCollection& InCollection, const int32 InitialSize = 0);
		FMetaInfoFacade(const FManagedArrayCollection& InCollection);

		bool IsConst() const { return Collection == nullptr; }
		bool IsValid() const;

		void CreateGuid(const FString& InPath);
		FGuid GetGuid() const;

	protected:
		void DefineSchema(const int32 InitialSize = 0);

		const FManagedArrayCollection& ConstCollection;
		FManagedArrayCollection* Collection = nullptr;
		
		TManagedArrayAccessor<FGuid> GuidAttribute;
	};
}
