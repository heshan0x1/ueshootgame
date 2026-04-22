// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTypeManager.h"
#include "MassEntityManager.h"
#include "MassTestableEnsures.h"
#include "MassEntityElementTypes.h"
#include "MassEntityRelations.h"
#include "Misc/CoreDelegates.h"


namespace UE::Mass
{
	//-----------------------------------------------------------------------------
	// FTypeHandle
	//-----------------------------------------------------------------------------
	FTypeHandle::FTypeHandle(TObjectKey<const UStruct> InTypeKey)
		: TypeKey(InTypeKey)
	{
		
	}

	//-----------------------------------------------------------------------------
	// FTypeManager
	//-----------------------------------------------------------------------------
	FTypeManager::FOnRegisterBuiltInTypes FTypeManager::OnRegisterBuiltInTypes;

	FTypeManager::FTypeManager(FMassEntityManager& InEntityManager)
		: OuterEntityManager(InEntityManager)
	{
	}

	void FTypeManager::RegisterBuiltInTypes()
	{
		OnRegisterBuiltInTypes.Broadcast(*this);
		bBuiltInTypesRegistered = true;
	}

	FTypeHandle FTypeManager::RegisterTypeInternal(TNotNull<const UStruct*> InType, FTypeInfo&& TypeInfo)
	{
		FTypeHandle TypeHandle(InType);
		FTypeInfo* ExistingData = TypeDataMap.Find(FTypeHandle(TypeHandle.TypeKey));
		if (LIKELY(ExistingData == nullptr))
		{
			if (TypeInfo.Traits.IsType<FSubsystemTypeTraits>())
			{
				SubsystemTypes.Add(FTypeHandle(TypeHandle.TypeKey));
			}

			TypeDataMap.Add(FTypeHandle(TypeHandle.TypeKey), MoveTemp(TypeInfo));
		}
		else if (testableEnsureMsgf(bBuiltInTypesRegistered == false, TEXT("Registered type overriding is supported only as part of built-in types registration.")))
		{
			// we're overriding the existing data with the new data in assumption it's more up-to-date.
			// The most common occurence of this will be with already registered subsystems' subclasses.
			// The subclasses can change the data registered on their behalf by the super class,
			// but most of the time that won't be necessary.
			*ExistingData = MoveTemp(TypeInfo);
		}
		
		return TypeHandle;
	}

	FTypeHandle FTypeManager::RegisterType(TNotNull<const UStruct*> InType, FSubsystemTypeTraits&& TypeTraits)
	{
		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();
		TypeInfo.Traits.Set<FSubsystemTypeTraits>(MoveTemp(TypeTraits));
		return RegisterTypeInternal(InType, MoveTemp(TypeInfo));
	}

	FTypeHandle FTypeManager::RegisterType(TNotNull<const UStruct*> InType, FSharedFragmentTypeTraits&& TypeTraits)
	{
		testableCheckfReturn(UE::Mass::IsA<FMassSharedFragment>(InType), {}
			, TEXT("%s is not a valid shared fragment type"), *InType->GetName());

		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();
		TypeInfo.Traits.Set<FSharedFragmentTypeTraits>(MoveTemp(TypeTraits));
		return RegisterTypeInternal(InType, MoveTemp(TypeInfo));
	}

	FTypeHandle FTypeManager::RegisterType(FRelationTypeTraits&& TypeTraits)
	{
		TNotNull<const UScriptStruct*> InType = TypeTraits.GetRelationTagType();
		testableCheckfReturn(UE::Mass::IsA<FMassRelation>(InType), return {}
			, TEXT("%s is not a valid relation type"), *InType->GetName());

		testableCheckfReturn(TypeTraits.RelationFragmentType->IsChildOf(FMassRelationFragment::StaticStruct()), return {}
			, TEXT("%s is not a valid TypeTraits.RelationFragmentType, needs to derive from FMassRelationFragment")
			, *TypeTraits.RelationFragmentType->GetName());

		if (TypeTraits.RelationName.IsNone())
		{
			TypeTraits.RelationName = InType->GetFName();
			TypeTraits.SetDebugInFix(TypeTraits.RelationName.ToString());
		}

		FTypeInfo* ExistingData = TypeDataMap.Find(FTypeHandle(InType));
		
		if (!testableEnsureMsgf(bBuiltInTypesRegistered == false || ExistingData == nullptr
			, TEXT("Modifying relationship after registration done is not supported")))
		{
			return {};
		}

		FTypeInfo TypeInfo;
		TypeInfo.TypeName = InType->GetFName();
		if (TypeTraits.RegisteredGroupType.IsValid() == false)
		{
			TypeTraits.RegisteredGroupType = OuterEntityManager.FindOrAddArchetypeGroupType(TypeTraits.RelationName);
		}
		TypeInfo.Traits.Set<FRelationTypeTraits>(MoveTemp(TypeTraits));

		const FTypeHandle RegisteredTypeHandle = RegisterTypeInternal(InType, MoveTemp(TypeInfo));
		if (RegisteredTypeHandle.IsValid())
		{
			if (bBuiltInTypesRegistered)
			{
				// if the built-in types are already registered we need to notify the entity manager that there's a new
				// relation type, that might require additional handling (like creation of appropriate entity destruction observers)
				// Note that we don't call this during built-in types registration to give project-specific code a chance
				// to override type traits before the entity manager handles the registered types.
				OuterEntityManager.OnNewTypeRegistered(RegisteredTypeHandle);
			}
		}

		return RegisteredTypeHandle;
	}

	FTypeHandle FTypeManager::GetRelationTypeHandle(const TNotNull<const UScriptStruct*> RelationOrElementType) const
	{
		const bool bValidRelationshipType = UE::Mass::IsA<FMassRelation>(RelationOrElementType);
		ensureMsgf(bValidRelationshipType, TEXT("%s is not a valid relationship type"), *RelationOrElementType->GetName());

		return UE::Mass::IsA<FMassRelation>(RelationOrElementType) 
			? FTypeHandle(RelationOrElementType)
			: FTypeHandle();
	}

	bool FTypeManager::IsValidRelationType(const TNotNull<const UScriptStruct*> RelationOrElementType) const
	{
		return UE::Mass::IsA<FMassRelation>(RelationOrElementType) && TypeDataMap.Contains(FTypeHandle(RelationOrElementType));
	}
}