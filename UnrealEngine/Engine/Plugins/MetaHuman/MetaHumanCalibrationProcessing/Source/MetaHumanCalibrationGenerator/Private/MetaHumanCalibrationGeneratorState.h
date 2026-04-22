// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCalibrationGeneratorOptions.h"
#include "MetaHumanCalibrationGeneratorConfig.h"
#include "MetaHumanCalibrationPatternDetector.h"

#include "Templates/SharedPointer.h"
#include "Containers/UnrealString.h"

class FMetaHumanCalibrationGeneratorState
{
public:

	TStrongObjectPtr<UMetaHumanCalibrationGeneratorConfig> Config;
	TStrongObjectPtr<UMetaHumanCalibrationGeneratorOptions> Options;
};