// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/InterchangeFactoryBaseNode.h"
#include "Sound/SoundWave.h"

#include "InterchangeAudioSoundWaveFactoryNode.generated.h"

UCLASS(MinimalAPI, BlueprintType)
class UInterchangeAudioSoundWaveFactoryNode : public UInterchangeFactoryBaseNode
{
	GENERATED_BODY()

public:
	virtual FString GetTypeName() const override
	{
		return TEXT("SoundWaveFactoryNode");
	}

	virtual UClass* GetObjectClass() const override
	{
		return USoundWave::StaticClass();
	}
};
