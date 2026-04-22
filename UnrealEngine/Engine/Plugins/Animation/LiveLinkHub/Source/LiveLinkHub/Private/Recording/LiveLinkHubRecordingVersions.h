// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace UE::LiveLinkHub::Private::RecordingVersions
{
	constexpr int32 InitialVersion = 1;
	/** Account for varying frame sizes and offsets. */
	constexpr int32 DynamicFrameSizes = 2;
	/** Timestamps serialized in bulk. */
	constexpr int32 BulkTimestamps = 3;

	/** The latest recording version. UPDATE IF ADDING NEW VERSIONS! */
	constexpr int32 Latest = BulkTimestamps;
}
