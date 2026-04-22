// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"

namespace Audio
{
	SIGNALPROCESSING_API void EncodeMonoAmbisonicMixIn(TArrayView<const float> Src, TArrayView<float*> Dst, TArrayView<const float> AmbisonicGains);
	SIGNALPROCESSING_API void DecodeMonoAmbisonicMixIn(TArrayView<const float*> Src, TArrayView<float> Dst, TArrayView<const float> AmbisonicGains);
}