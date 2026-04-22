// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Misc/EnumClassFlags.h"

class UObject;

namespace PlainProps::UE
{

enum class EBindMode : uint8 { All, Source, Runtime };

enum class ERoundtrip : uint8
{
	None 		= 0,
	PP 			= 1 << 0,
	TPS 		= 1 << 1,
	UPS 		= 1 << 2,
	TextMemory	= 1 << 3,
	TextStable	= 1 << 4,
};
ENUM_CLASS_FLAGS(ERoundtrip);

PLAINPROPSUOBJECT_API void SchemaBindAllTypes(EBindMode Mode);
PLAINPROPSUOBJECT_API int32 RoundtripViaBatch(TConstArrayView<UObject*> Objects, ERoundtrip Options);
PLAINPROPSUOBJECT_API int32 RoundtripViaPackages(TConstArrayView<UObject*> Objects, ERoundtrip Options);

}
