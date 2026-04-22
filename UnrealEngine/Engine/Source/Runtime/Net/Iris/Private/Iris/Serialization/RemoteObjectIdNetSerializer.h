// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "RemoteObjectIdNetSerializer.generated.h"

USTRUCT()
struct FRemoteObjectIdNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	UE_NET_DECLARE_SERIALIZER_INTERNAL(FRemoteObjectIdNetSerializer, IRISCORE_API);
}
