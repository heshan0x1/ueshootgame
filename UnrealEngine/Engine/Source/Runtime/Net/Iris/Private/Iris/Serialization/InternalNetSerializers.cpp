// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InternalNetSerializers)

FArrayPropertyNetSerializerConfig::FArrayPropertyNetSerializerConfig()
: FNetSerializerConfig()
{
	ConfigTraits = ENetSerializerConfigTraits::NeedDestruction;
}

FArrayPropertyNetSerializerConfig::~FArrayPropertyNetSerializerConfig() = default;
