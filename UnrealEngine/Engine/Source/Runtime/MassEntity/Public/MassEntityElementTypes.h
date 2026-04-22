// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityElementTypes.generated.h"

// This is the base class for all lightweight fragments
USTRUCT()
struct FMassFragment
{
	GENERATED_BODY()
};

// these are the messages we'll print out when static checks whether a given type is a fragment fails
#define _MASS_INVALID_FRAGMENT_CORE_MESSAGE "Make sure to inherit from FMassFragment or one of its child-types and ensure that the struct is trivially copyable, or opt out by specializing TMassFragmentTraits for this type and setting AuthorAcceptsItsNotTriviallyCopyable = true"
#define MASS_INVALID_FRAGMENT_MSG  "Given struct doesn't represent a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE
#define MASS_INVALID_FRAGMENT_MSG_F  "Type %s is not a valid fragment type." _MASS_INVALID_FRAGMENT_CORE_MESSAGE


// This is the base class for types that will only be tested for presence/absence, i.e. Tags.
// Subclasses should never contain any member properties.
USTRUCT()
struct FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FMassChunkFragment
{
	GENERATED_BODY()
};

USTRUCT()
struct FMassSharedFragment
{
	GENERATED_BODY()
};

USTRUCT()
struct FMassConstSharedFragment
{
	GENERATED_BODY()
};

namespace UE::Mass
{
	template<typename T>
	bool IsA(const UStruct* /*Struct*/)
	{
		return false;
	}

	template<>
	inline bool IsA<FMassFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassFragment::StaticStruct());
	}

	template<>
	inline bool IsA<FMassTag>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassTag::StaticStruct());
	}

	template<>
	inline bool IsA<FMassChunkFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassChunkFragment::StaticStruct());
	}

	template<>
	inline bool IsA<FMassSharedFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassSharedFragment::StaticStruct());
	}

	template<>
	inline bool IsA<FMassConstSharedFragment>(const UStruct* Struct)
	{
		return Struct && Struct->IsChildOf(FMassConstSharedFragment::StaticStruct());
	}
}
