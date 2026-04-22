// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

#define UE_API METAHUMANCAPTUREUTILS_API

class FStopToken
{
public:
	// We can't deprecate the type itself without encountering seemingly impossible to suppress warnings inside TTuple
	// (due to its usage in various maps, which can't be easily removed or replaced), so we deprecate the constructor
	// to at least provide some degree of warning
	UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureUtils is deprecated. This functionality is now available in the CaptureManagerCore/CaptureUtils module")
		UE_API FStopToken();

	UE_API void RequestStop();
	UE_API bool IsStopRequested() const;

private:

	struct FSharedState
	{
		std::atomic_bool State = false;
	};

	TSharedPtr<FSharedState> SharedState;
};

#undef UE_API
