// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleMetadataProcessor.h"

FChaosVDParticleMetadataProcessor::FChaosVDParticleMetadataProcessor() : FChaosVDDataProcessorBase(FChaosVDParticleMetadata::WrapperTypeName)
{
}

bool FChaosVDParticleMetadataProcessor::ProcessRawData(const TArray<uint8>& InData)
{
	FChaosVDDataProcessorBase::ProcessRawData(InData);

	TSharedPtr<FChaosVDTraceProvider> ProviderSharedPtr = TraceProvider.Pin();
	if (!ensure(ProviderSharedPtr.IsValid()))
	{
		return false;
	}

	TSharedPtr<FChaosVDParticleMetadata> ParticleMetaData = MakeShared<FChaosVDParticleMetadata>();
	const bool bSuccess = Chaos::VisualDebugger::ReadDataFromBuffer(InData, *ParticleMetaData, ProviderSharedPtr.ToSharedRef());

	if (bSuccess)
	{
		ProviderSharedPtr->AddParticleMetadata(ParticleMetaData->MetadataID, ParticleMetaData);
	}

	return bSuccess;
}
