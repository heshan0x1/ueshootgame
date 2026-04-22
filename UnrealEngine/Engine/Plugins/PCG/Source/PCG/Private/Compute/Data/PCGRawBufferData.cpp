// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compute/Data/PCGRawBufferData.h"

#include "PCGModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGRawBufferData)

PCG_DEFINE_TYPE_INFO(FPCGDataTypeInfoRawBuffer, UPCGRawBufferData)

void UPCGRawBufferData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	// This data does not have a bespoke CRC implementation so just use the unique object instance UID.
	AddUIDToCrc(Ar);
}
