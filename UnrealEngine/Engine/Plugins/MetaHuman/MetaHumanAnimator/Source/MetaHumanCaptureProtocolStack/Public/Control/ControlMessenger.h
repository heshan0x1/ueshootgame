// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utility/Definitions.h"

#include "Control/Communication/ControlCommunication.h"
#include "Control/Communication/ControlPacket.h"

#include "Messages/ControlMessage.h"
#include "Messages/ControlRequest.h"
#include "Messages/ControlResponse.h"
#include "Messages/ControlUpdate.h"

#include "Utility/Error.h"

#include "Misc/ScopedEvent.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"

#define UE_API METAHUMANCAPTUREPROTOCOLSTACK_API

class FKeepAliveCounter
{
public:
	FKeepAliveCounter();

	void Increment();
	void Reset();
	bool HasReached(uint16 InBound);

private:

	std::atomic<uint16> Counter;
};

class UE_DEPRECATED(5.7, "MetaHumanAnimator/MetaHumanCaptureProtocolStack is deprecated. This functionality is now available in the CaptureManagerCore/CaptureProtocolStack module")
	FControlMessenger final
{
public:

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    template<class RequestType>
    using FOnControlResponse = TDelegate<void(TProtocolResult<typename RequestType::ResponseType> InResult)>;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	DECLARE_DELEGATE_OneParam(FOnDisconnect, const FString& InCause);

    static constexpr int32 ResponseWaitTime = 3; // Seconds
    static constexpr int32 KeepAliveInterval = 5; // Seconds
    static UE_API const TCHAR* HandshakeSessionId;

    UE_API FControlMessenger();
    UE_API ~FControlMessenger();
    
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    UE_API void RegisterUpdateHandler(FString InAddressPath, FControlUpdate::FOnUpdateMessage InUpdateHandler);
	UE_API void RegisterDisconnectHandler(FOnDisconnect InOnDisconnectHandler);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	UE_API TProtocolResult<void> Start(const FString& InServerIp, const uint16 InServerPort);
	UE_API TProtocolResult<void> Stop();

	UE_API TProtocolResult<void> StartSession();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    UE_API TProtocolResult<FGetServerInformationResponse> GetServerInformation();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

    template<class RequestType>
	TProtocolResult<typename RequestType::ResponseType> SendRequest(RequestType InRequest)
    {
        FControlMessage Message(InRequest.GetAddressPath(), FControlMessage::EType::Request, InRequest.GetBody());

        uint32 TransactionId = GenerateTransactionId();

        {
            FScopeLock SessionLock(&SessionIdMutex);
            Message.SetSessionId(SessionId);
        }
        
        Message.SetTransactionId(TransactionId);
        Message.SetTimestamp(GetTimestamp());
        
		TProtocolResult<FControlPacket> SerializeResult = FControlMessage::Serialize(Message);

        if (SerializeResult.IsError())
        {
            return FCaptureProtocolError(TEXT("Failed to serialize a request."));
        }

        TPromise<TProtocolResult<FControlMessage>> Promise;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        TFuture<TProtocolResult<FControlMessage>> Future = Promise.GetFuture();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

        {
            FScopeLock Lock(&RequestsMutex);
            RequestContexts.Emplace(TransactionId, MakeUnique<FRequestContext>(MoveTemp(Message), MoveTemp(Promise)));
        }

        SendPacket(SerializeResult.ClaimResult());

        if (!Future.WaitFor(FTimespan::FromSeconds(ResponseWaitTime)))
        {
            FScopeLock Lock(&RequestsMutex);

            const TUniquePtr<FRequestContext>& RequestContext = RequestContexts[TransactionId];

			RequestContext->Promise.SetValue(FCaptureProtocolError("Broken promise")); // Actual error to avoid the destructor error.

            RequestContexts.Remove(TransactionId);

            return FCaptureProtocolError(TEXT("Server failed to respond within 3 seconds."));
        }

        FScopeLock Lock(&RequestsMutex);
        const TUniquePtr<FRequestContext>& RequestContext = RequestContexts[TransactionId];

        const TProtocolResult<FControlMessage>& FutureResult = Future.Get();
        check(FutureResult.IsValid());

        const FControlMessage& ResponseMessage = FutureResult.GetResult();

        {
            FScopeLock SessionLock(&SessionIdMutex);
            if (ResponseMessage.GetSessionId() != SessionId)
            {
                return FCaptureProtocolError(TEXT("Invalid session ID arrived"));
            }
        }

        if (!ResponseMessage.GetErrorName().IsEmpty())
        {
            return FCaptureProtocolError(TEXT("Server respond with error: ") + ResponseMessage.GetErrorName());
        }

        typename RequestType::ResponseType Response;
        RequestContexts.Remove(TransactionId);

		TProtocolResult<void> ParseResult = Response.Parse(ResponseMessage.GetBody());
        if (ParseResult.IsError())
        {
            return FCaptureProtocolError(TEXT("Failed to parse the response: ") + ParseResult.ClaimError().GetMessage());
        }
        
        return Response;
    }

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    template<class RequestType>
    void SendAsyncRequest(RequestType InRequest, FOnControlResponse<RequestType> InOnResponse)
    {
		AsyncRequestRunner.Add(FAsyncRequestDelegate::CreateLambda([this, Request(MoveTemp(InRequest)), OnResponse(MoveTemp(InOnResponse))]() mutable
		{
			TProtocolResult<typename RequestType::ResponseType> Result = SendRequest(MoveTemp(Request));

			OnResponse.ExecuteIfBound(MoveTemp(Result));
		}));
    }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

private:
	DECLARE_DELEGATE(FAsyncRequestDelegate)

    struct FRequestContext
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        FRequestContext(FControlMessage InRequest, TPromise<TProtocolResult<FControlMessage>> InPromise)
            : Request(MoveTemp(InRequest))
            , Promise(MoveTemp(InPromise))
        {
        }

        FControlMessage Request;
        TPromise<TProtocolResult<FControlMessage>> Promise;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    };

    UE_API void SendPacket(FControlPacket InPacket);
    UE_API void KeepAlive();
    UE_API void MessageHandler(FControlPacket InPacket);

	UE_API uint32 GenerateTransactionId() const;
	UE_API uint64 GetTimestamp() const;

    UE_API void StartKeepAliveTimer();
    UE_API void StopKeepAliveTimer();

	UE_API void OnAsyncRequestProcess(FAsyncRequestDelegate InAsyncDelegate);

    FControlCommunication Communication;

    FCriticalSection SessionIdMutex;
    FString SessionId;

    FCriticalSection RequestsMutex;
    TMap<uint32, TUniquePtr<FRequestContext>> RequestContexts;

    FCriticalSection UpdatesMutex;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    TMap<FString, FControlUpdate::FOnUpdateMessage> UpdateHandlers;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FTSTicker::FDelegateHandle KeepAliveTimer;
	FKeepAliveCounter KeepAliveFailures;

	TQueueRunner<FAsyncRequestDelegate> AsyncRequestRunner;
	FOnDisconnect OnDisconnectHandler;

	FRandomStream RandomStream;
};

#undef UE_API
