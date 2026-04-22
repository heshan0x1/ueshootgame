// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utility/Error.h"

FCaptureProtocolError::FCaptureProtocolError()
    : Message("")
    , Code(0)
{
}

FCaptureProtocolError::FCaptureProtocolError(FString InMessage, int32 InCode)
    : Message(MoveTemp(InMessage))
    , Code(InCode)
{
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
const FString& FCaptureProtocolError::GetMessage() const
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
    return Message;
}

int32 FCaptureProtocolError::GetCode() const
{
    return Code;
}