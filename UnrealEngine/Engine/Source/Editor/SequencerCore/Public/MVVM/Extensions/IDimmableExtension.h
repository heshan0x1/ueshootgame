// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/ViewModelTypeID.h"

#define UE_API SEQUENCERCORE_API

namespace UE
{
namespace Sequencer
{

class IDimmableExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID_API(UE_API, IDimmableExtension)

	virtual ~IDimmableExtension(){}

	virtual bool IsDimmed() const = 0;
};

} // namespace Sequencer
} // namespace UE

#undef UE_API
