// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "RepMovementNetSerializer.generated.h"

USTRUCT()
struct FRepMovementNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};


namespace UE::Net
{

UE_NET_DECLARE_SERIALIZER(FRepMovementNetSerializer, ENGINE_API);

}

