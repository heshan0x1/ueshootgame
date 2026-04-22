// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModelTypeID.h"

#define UE_API SEQUENCER_API

struct FGuid;

namespace UE
{
namespace Sequencer
{

class IObjectBindingExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID_API(UE_API, IObjectBindingExtension)

	virtual ~IObjectBindingExtension(){}

	virtual FGuid GetObjectGuid() const = 0;
};

} // namespace Sequencer
} // namespace UE

#undef UE_API
