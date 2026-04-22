// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantWebJavaScriptResultDelegate.h"

#include "Async/UniqueLock.h"
#include "Templates/SharedPointer.h"

// Name of this object when registered with the JavaScript binder.
// NOTE: This is lower case as FCEFJSScripting::GetBindingName() will silently change the name
// to lower case if FCEFJSScripting::bJSBindingToLoweringEnabled is true (the default).
const FString UAIAssistantWebJavaScriptResultDelegate::BaseName(TEXT("aiassistantresultdelegate"));
// Result set when a promise is canceled.
const FString UAIAssistantWebJavaScriptResultDelegate::CanceledError(
	TEXT(R"json("canceled")json"));

void UAIAssistantWebJavaScriptResultDelegate::Bind(
	UE::AIAssistant::IWebJavaScriptDelegateBinder& WebJavaScriptDelegateBinder)
{
	ScopedWebJavaScriptDelegateBinder.Emplace(
		WebJavaScriptDelegateBinder, GetName(), this, true /* bIsPermanent */);
}

void UAIAssistantWebJavaScriptResultDelegate::Unbind()
{
	ScopedWebJavaScriptDelegateBinder.Reset();
	CompleteAllPendingPromises();
}

FString UAIAssistantWebJavaScriptResultDelegate::CreateHandlerId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
}

void UAIAssistantWebJavaScriptResultDelegate::RegisterResultHandlerWithId(
	FResultHandler&& Handler, const FString& HandlerId)
{
	UE::TUniqueLock Lock(ResultHandlersByIdLock);
	ResultHandlersById.Emplace(HandlerId, MakeShared<FResultHandler>(MoveTemp(Handler)));
}

FString UAIAssistantWebJavaScriptResultDelegate::RegisterResultHandler(FResultHandler&& Handler)
{
	FString HandlerId = CreateHandlerId();
	RegisterResultHandlerWithId(MoveTemp(Handler), HandlerId);
	return HandlerId;
}

TPair<FString, TFuture<UAIAssistantWebJavaScriptResultDelegate::FResult>>
UAIAssistantWebJavaScriptResultDelegate::RegisterResultHandlerForFuture()
{
	FString HandlerId = CreateHandlerId();
	TFuture<FResult> Future = CreatePromiseAndGetFuture(HandlerId);
	RegisterResultHandlerWithId(
		[this](FResultHandlerContext&& ResultHandlerContext) mutable -> bool
		{
			TryCompletePendingPromise(MoveTemp(ResultHandlerContext));
			return true;
		},
		HandlerId);
	return TPair<FString, TFuture<FResult>>(MoveTemp(HandlerId), MoveTemp(Future));
}

void UAIAssistantWebJavaScriptResultDelegate::HandleResult(
	const FString& HandlerId, const FString& ResultJson, bool bResultJsonIsError)
{
	TSharedPtr<FResultHandler> Handler;
	{
		UE::TUniqueLock Lock(ResultHandlersByIdLock);
		TSharedPtr<FResultHandler>* HandlerPtrPtr = ResultHandlersById.Find(HandlerId);
		if (!HandlerPtrPtr) return;
		Handler = *HandlerPtrPtr;
	}
	bool RemoveHandler =
		(*Handler)(FResultHandlerContext{ { ResultJson, bResultJsonIsError }, HandlerId });
	if (RemoveHandler)
	{
		UE::TUniqueLock Lock(ResultHandlersByIdLock);
		ResultHandlersById.Remove(HandlerId);
	}
}

FString UAIAssistantWebJavaScriptResultDelegate::FormatJavaScriptHandler(const FString& HandlerId) const
{
	return FString::Printf(
		TEXT(R"js(window.ue.%s.%s("%s", JSON.stringify({0}), {1});)js"),
		*GetName(),
		*GET_FUNCTION_NAME_CHECKED(
			UAIAssistantWebJavaScriptResultDelegate, HandleResult).ToString().ToLower(),
		*HandlerId);
}

const FString& UAIAssistantWebJavaScriptResultDelegate::GetName() const
{
	UE::TUniqueLock Lock(NameLock);
	if (Name.IsEmpty())
	{
		Name = FString::Printf(
			TEXT("%s_%s"), *BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::DigitsLower));
	}
	return Name;
}

void UAIAssistantWebJavaScriptResultDelegate::BeginDestroy()
{
	Super::BeginDestroy();
	Unbind();
}

void UAIAssistantWebJavaScriptResultDelegate::CompleteAllPendingPromises()
{
	UE::TUniqueLock Lock(PendingPromisesByIdLock);
	for (auto& HandlerIdAndPromise : PendingPromisesById)
	{
		HandlerIdAndPromise.Value.SetValue(FResult{ CanceledError, true });
	}
	PendingPromisesById.Empty();
}

TFuture<UAIAssistantWebJavaScriptResultDelegate::FResult>
UAIAssistantWebJavaScriptResultDelegate::CreatePromiseAndGetFuture(
	const FString& HandlerId)
{
	UE::TUniqueLock Lock(PendingPromisesByIdLock);
	return PendingPromisesById.Add(HandlerId).GetFuture();
}

void UAIAssistantWebJavaScriptResultDelegate::TryCompletePendingPromise(
	FResultHandlerContext&& ResultHandlerContext)
{
	bool bFulfillPromise = false;
	TPromise<FResult> PromiseToFulfill;
	{
		UE::TUniqueLock Lock(PendingPromisesByIdLock);
		FString HandlerId = ResultHandlerContext.HandlerId;
		auto* Promise = PendingPromisesById.Find(HandlerId);
		if (Promise)
		{
			PromiseToFulfill = MoveTemp(*Promise);
			bFulfillPromise = true;
			PendingPromisesById.Remove(HandlerId);
		}
	}
	if (bFulfillPromise)
	{
		// NOTE: If PendingPromisesByIdLock is held when this is set any calls to register
		// handlers will attempt to acquire PendingPromisesByIdLock and deadlock.
		PromiseToFulfill.SetValue(MoveTemp(ResultHandlerContext));
	}
}