// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Particles meta data
 */
class FChaosVDParticleMetadataProcessor final : public FChaosVDDataProcessorBase
{
public:
	virtual ~FChaosVDParticleMetadataProcessor() override = default;
	explicit FChaosVDParticleMetadataProcessor();

	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
