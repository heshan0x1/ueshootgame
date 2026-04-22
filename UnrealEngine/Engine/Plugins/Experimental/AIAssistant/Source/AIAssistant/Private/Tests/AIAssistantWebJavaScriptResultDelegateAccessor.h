// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

#include "AIAssistantWebJavaScriptResultDelegate.h"

namespace UE::AIAssistant
{
	class FWebJavaScriptResultDelegateAccessor
	{
	public:
		// Call Delegate.HandleResult().
		static void CallHandleResult(
			UAIAssistantWebJavaScriptResultDelegate& Delegate,
			const FString& HandlerId, const FString& ResultJson, bool bResultJsonIsError)
		{
			Delegate.HandleResult(HandlerId, ResultJson, bResultJsonIsError);
		}

		static const TMap<
			FString, TSharedPtr<UAIAssistantWebJavaScriptResultDelegate::FResultHandler>>&
			GetResultHandlersById(UAIAssistantWebJavaScriptResultDelegate& Delegate)
		{
			return Delegate.ResultHandlersById;
		}
	};
}