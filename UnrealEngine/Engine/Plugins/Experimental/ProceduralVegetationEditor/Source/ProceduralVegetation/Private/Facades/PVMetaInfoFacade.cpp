// Copyright Epic Games, Inc. All Rights Reserved.

#include "Facades/PVMetaInfoFacade.h"
#include "Facades/PVAttributesNames.h"

namespace PV::Facades
{
	FMetaInfoFacade::FMetaInfoFacade(FManagedArrayCollection& InCollection, const int32 InitialSize)
		: ConstCollection(InCollection)
		, Collection(&InCollection)
		, GuidAttribute(InCollection, AttributeNames::Guid, GroupNames::DetailsGroup)
	{
		DefineSchema(InitialSize);
	}

	FMetaInfoFacade::FMetaInfoFacade(const FManagedArrayCollection& InCollection)
		: ConstCollection(InCollection)
		, Collection(nullptr)
		, GuidAttribute(InCollection, AttributeNames::Guid, GroupNames::DetailsGroup)
	{}

	void FMetaInfoFacade::DefineSchema(const int32 InitialSize)
	{
		check(!IsConst());

		if (!Collection->HasGroup(GroupNames::DetailsGroup))
		{
			Collection->AddGroup(GroupNames::DetailsGroup);
		}

		GuidAttribute.Add();
	}

	bool FMetaInfoFacade::IsValid() const
	{
		return GuidAttribute.IsValid();
	}

	void FMetaInfoFacade::CreateGuid(const FString& InPath)
	{
		check(!IsConst());

		int32 NumElements = Collection->NumElements(GroupNames::DetailsGroup);

		if(NumElements == 0)
		{
			Collection->AddElements(1, GroupNames::DetailsGroup);
		}
		
		GuidAttribute.Modify()[0] = FGuid::NewDeterministicGuid(InPath);
	}

	FGuid FMetaInfoFacade::GetGuid() const
	{
		if(GuidAttribute.IsValid() && GuidAttribute.IsValidIndex(0))
		{
			return GuidAttribute[0];
		}

		return FGuid::NewGuid();
	}
}
