// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <atomic>
#include "Containers/MpscQueue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Logging/StructuredLog.h"
#include "Misc/SingleThreadRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "UdpMessagingPrivate.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"


namespace UE::UdpMessaging {

/**
 * Asynchronously sends data to a UDP socket.
 */
class FSocketSender
	: public FRunnable
	, private FSingleThreadRunnable
{
	// Structure for outbound packets.
	struct FPacket
	{
		/** Holds the packet's data. */
		TSharedPtr<TArray<uint8>> Data;

		/** Holds the recipient. */
		FIPv4Endpoint Recipient;

		/** Default constructor. */
		FPacket() { }

		/** Creates and initializes a new instance. */
		FPacket(const TSharedRef<TArray<uint8>>& InData, const FIPv4Endpoint& InRecipient)
			: Data(InData)
			, Recipient(InRecipient)
		{ }
	};

	enum class EUpdateResult
	{
		/** The queued was emptied, and we can await new work. */
		Done,

		/** There was a transient failure, and we should attempt to service the queue again soon. */
		Retry,

		/** Tear down this FSocketSender and percolate the failure to the caller. */
		Fatal,
	};

public:

	struct FOptions
	{
		int32 SendBufferSize = 512 * 1024;
		FTimespan WaitTime = FTimespan::FromMilliseconds(100);

		FOptions()
		{
		}
	};

	/**
	 * Creates and initializes a new socket sender.
	 *
	 * @param InSocket The UDP socket to use for sending data.
	 * @param InDescription The description text (for debugging).
	 */
	FSocketSender(FSocket* InSocket, const TCHAR* InDescription, const FOptions& InOptions = FOptions())
		: Socket(InSocket)
		, SocketSubsystem(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		, bStopping(false)
		, WaitTime(InOptions.WaitTime)
		, Description(InDescription)
	{
		check(Socket != nullptr);
		check(Socket->GetSocketType() == SOCKTYPE_Datagram);

		int32 NewSize = 0;
		Socket->SetSendBufferSize(InOptions.SendBufferSize, NewSize);

		WorkEvent = FPlatformProcess::GetSynchEventFromPool();
		Thread = FRunnableThread::Create(this, InDescription, 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
	}

	/** Virtual destructor. */
	virtual ~FSocketSender()
	{
		if (Thread != nullptr)
		{
			Thread->Kill(true);
			delete Thread;
		}

		FPlatformProcess::ReturnSynchEventToPool(WorkEvent);
		WorkEvent = nullptr;
	}

public:

	/**
	 * Sends data to the specified recipient.
	 *
	 * @param Data The data to send.
	 * @param Recipient The recipient.
	 * @return true if the data will be sent, false otherwise.
	 */
	bool Send(const TSharedRef<TArray<uint8>, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& Recipient)
	{
		if (!bStopping)
		{
			SendQueue.Enqueue(Data, Recipient);
			WorkEvent->Trigger();
			return true;
		}

		return false;
	}

public:

	//~ FRunnable interface

	virtual FSingleThreadRunnable* GetSingleThreadInterface() override
	{
		return this;
	}

	virtual bool Init() override
	{
		return true;
	}

	virtual uint32 Run() override
	{
		while (!bStopping)
		{
			const EUpdateResult Result = Update(WaitTime);
			switch (Result)
			{
				case EUpdateResult::Done:
					WorkEvent->Wait(WaitTime);
					continue;
				case EUpdateResult::Retry:
					continue;
				case EUpdateResult::Fatal:
					bStopping = true;
					return 0;
			}
		}

		return 0;
	}

	virtual void Stop() override
	{
		bStopping = true;
		WorkEvent->Trigger();
	}

	virtual void Exit() override { }

protected:

	/**
	 * Update this socket sender.
	 *
	 * @param Time to wait for the socket.
	 * @return true on success, false otherwise.
	 */
	EUpdateResult Update(const FTimespan& SocketWaitTime)
	{
		while (!SendQueue.IsEmpty())
		{
			if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, SocketWaitTime))
			{
				return EUpdateResult::Retry;
			}

			const FPacket* Packet = SendQueue.Peek();
			const FIPv4Endpoint Recipient = Packet->Recipient;
			const int32 PacketNumBytes = Packet->Data->Num();
			int32 SentNumBytes = 0;
			const bool bSendResult =
				Socket->SendTo(Packet->Data->GetData(), PacketNumBytes, SentNumBytes, *Recipient.ToInternetAddr());

			ESocketErrors SocketError = SE_NO_ERROR;

			if (!bSendResult)
			{
				SocketError = SocketSubsystem->GetLastErrorCode();
				if (SocketError == SE_EWOULDBLOCK || SocketError == SE_ENOBUFS || SocketError == SE_EINTR)
				{
					// Leave the packet in the queue to try again.
					return EUpdateResult::Retry;
				}
			}

			// Only pop after we've checked for "retry-able" error codes.
			Packet = nullptr;
			SendQueue.Dequeue();

			if (!bSendResult)
			{
				bool bFatalError;
				switch (SocketError)
				{
					case SE_ENETDOWN:
					case SE_ENETUNREACH:
					case SE_EHOSTDOWN:
					case SE_EHOSTUNREACH:
					case SE_EADDRNOTAVAIL:
						bFatalError = false;
						break;
					default:
						bFatalError = true;
						break;
				}

				if (!bFatalError)
				{
					// Non-fatal; on to the next packet in the queue.
					UE_LOGFMT(LogUdpMessaging, Verbose,
						"Sender {Description}: SendTo failed (destination: {Recipient}) ({ErrorStr})",
						Description, Recipient.ToString(), SocketSubsystem->GetSocketError(SocketError));
					continue;
				}
				else
				{
					UE_LOGFMT(LogUdpMessaging, Error,
						"Sender {Description}: SendTo failed (destination: {Recipient}) ({ErrorStr})",
						Description, Recipient.ToString(), SocketSubsystem->GetSocketError(SocketError));
				}
			}

			if (SentNumBytes != PacketNumBytes)
			{
				if (SentNumBytes >= 0)
				{
					// FIXME?: In the absence of another socket error, could be a retry? Is this even possible for UDP?
					UE_LOGFMT(LogUdpMessaging, Error,
						"Sender {Description}: Incomplete send (destination: {Recipient}) ({Sent}/{Total} bytes)",
						Description, Recipient.ToString(), SentNumBytes, PacketNumBytes);
				}
				return EUpdateResult::Fatal;
			}
		}

		return EUpdateResult::Done;
	}

protected:

	//~ FSingleThreadRunnable interface

	virtual void Tick() override
	{
		Update(FTimespan::Zero());
	}

private:

	/** The send queue. */
	TMpscQueue<FPacket> SendQueue;

	/** The network socket. */
	FSocket* Socket;

	/** Cached pointer to the platform socket subsystem. */
	ISocketSubsystem* SocketSubsystem;

	/** Flag indicating that the thread is stopping. */
	std::atomic<bool> bStopping;

	/** The thread object. */
	FRunnableThread* Thread;

	/** Maximum time to wait for the socket to be ready to send. */
	FTimespan WaitTime;

	/** An event signaling that outbound messages need to be processed. */
	FEvent* WorkEvent;

	/** Description of this sender (for debugging). */
	FString Description;
};

} // namespace UE::UdpMessaging
