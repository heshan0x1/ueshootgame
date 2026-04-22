// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "Error/Result.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class FCaptureProtocolError
{
public:
	// We can't deprecate the type itself without encountering seemingly impossible to suppress warnings, so we 
	// deprecate the constructors to at least provide some degree of warning
	UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
	UE_API FCaptureProtocolError();

	UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module") 
	UE_API FCaptureProtocolError(FString InMessage, int32 InCode = 0);

	UE_API const FString& GetMessage() const;
	UE_API int32 GetCode() const;

private:

	FString Message;
	int32 Code;
};

template <typename ValueType>
using TProtocolResult = TResult<ValueType, FCaptureProtocolError>;

#define CPS_CHECK_VOID_RESULT(Function)                        \
if (TProtocolResult<void> Result = Function; Result.IsError()) \
{                                                              \
	return Result.ClaimError();                                \
}

#undef UE_API
