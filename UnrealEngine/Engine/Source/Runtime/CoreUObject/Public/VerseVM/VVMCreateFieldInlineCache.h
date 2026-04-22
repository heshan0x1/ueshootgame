// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMShape.h"

namespace Verse
{

struct FCreateFieldCacheCase
{
	enum class EKind : uint8
	{
		ValueObjectConstant,
		ValueObjectField,
		NativeStruct,
		UObject,
		Invalid
	};

	EKind Kind = EKind::Invalid;
	uint32 FieldIndex = 0;
	uint32 NextEmergentTypeOffset = 0;

	explicit operator bool() { return Kind != EKind::Invalid; }
};

} // namespace Verse
#endif // WITH_VERSE_VM
