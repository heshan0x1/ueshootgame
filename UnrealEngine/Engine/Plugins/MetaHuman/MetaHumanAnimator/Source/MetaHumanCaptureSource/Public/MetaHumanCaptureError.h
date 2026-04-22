// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#define UE_API METAHUMANCAPTURESOURCE_API

enum EMetaHumanCaptureError
{
	InvalidArguments,
	AbortedByUser,
	InvalidErrorCode,
	CommunicationError,
	NotFound,
	InternalError,
	Warning,
};

// This is designed to be used with TResult, which is why there is no "NoError" code
class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureSource is deprecated. This functionality is now available in the CaptureManager/CaptureManagerDevices module")
	FMetaHumanCaptureError
{
public:

	UE_API FMetaHumanCaptureError();
	UE_API FMetaHumanCaptureError(EMetaHumanCaptureError InCode, FString InMessage = TEXT(""));

	UE_API const FString& GetMessage() const;
	UE_API EMetaHumanCaptureError GetCode() const;

private:

	EMetaHumanCaptureError Code;
	FString Message;
};

#undef UE_API
