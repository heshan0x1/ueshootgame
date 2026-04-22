// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/FrameNumber.h"
#include "MVVM/ViewModelTypeID.h"

#define UE_API SEQUENCENAVIGATOR_API

namespace UE::SequenceNavigator
{

class IOutTimeExtension
{
public:
	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID_API(UE_API, IOutTimeExtension)

	virtual ~IOutTimeExtension() = default;

	virtual FFrameNumber GetOutTime() const = 0;

	virtual void SetOutTime(const FFrameNumber& InTime) = 0;
};

} // namespace UE::SequenceNavigator

#undef UE_API
