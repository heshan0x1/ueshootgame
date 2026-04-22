// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCaptureError.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

FMetaHumanCaptureError::FMetaHumanCaptureError() : Code(EMetaHumanCaptureError::InvalidErrorCode)
{

}

FMetaHumanCaptureError::FMetaHumanCaptureError(EMetaHumanCaptureError InCode, FString InMessage) :
	Code(InCode), Message(MoveTemp(InMessage))
{
}

const FString& FMetaHumanCaptureError::GetMessage() const
{
	return Message;
}

EMetaHumanCaptureError FMetaHumanCaptureError::GetCode() const
{
	return Code;
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS
