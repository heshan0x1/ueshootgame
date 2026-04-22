// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "RemoteObjectReferenceNetSerializer.generated.h"

USTRUCT()
struct FRemoteObjectReferenceNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	UE_NET_DECLARE_SERIALIZER_INTERNAL(FRemoteObjectReferenceNetSerializer, IRISCORE_API);
}
