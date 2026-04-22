// Copyright Epic Games, Inc. All Rights Reserved.

#include "Communication/TcpClient.h"

FTcpClient::FTcpClient() 
    : bRunning(false)
{
}

FTcpClient::~FTcpClient()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
    Stop();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TProtocolResult<void> FTcpClient::Init()
{
    if (bRunning)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Can't initialize the client while running."));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    // Prepare socket
	FSocket* RawSocket = FTcpSocketBuilder(TEXT("CPS TCP Socket"))
		.AsNonBlocking()
		.AsReusable()
		.WithReceiveBufferSize(BufferSize)
		.WithSendBufferSize(BufferSize)
		.Build();

	if (!RawSocket)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return FCaptureProtocolError(TEXT("Failed to create a client socket"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// Wrapping the socket in UniquePtr
    TcpSocket = FSocketPtr(RawSocket, FSocketDeleter(ISocketSubsystem::Get()));

    return ResultOk;
}

// Blocking function until connection is established
TProtocolResult<void> FTcpClient::Start(const FString& InServerAddress)
{
    if (bRunning)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("The client is already started"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    FIPv4Endpoint Endpoint;
    FIPv4Endpoint::FromHostAndPort(InServerAddress, Endpoint);

    bool IsConnected = TcpSocket->Connect(*Endpoint.ToInternetAddr());        

    // Wait 500 milliseconds before reporting an error
    FDateTime Time = FDateTime::UtcNow();
    while (IsConnected && TcpSocket->GetConnectionState() != SCS_Connected)
    {
        FPlatformProcess::Sleep(0.20f);
        if ((FDateTime::UtcNow() - Time).GetTotalMilliseconds() > 500)
        {
            IsConnected = false;
        }
    }

    if (!IsConnected)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Failed to connect the client"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    bRunning = true;

    return ResultOk;
}

TProtocolResult<void> FTcpClient::Stop()
{
    if (!bRunning)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("The client is already stopped"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

    TcpSocket->Shutdown(ESocketShutdownMode::ReadWrite);
    TcpSocket->Close();
    TcpSocket = nullptr;

    bRunning = false;

    return ResultOk;
}

bool FTcpClient::IsRunning() const
{
	return bRunning;
}

TProtocolResult<void> FTcpClient::SendMessage(const TArray<uint8>& InPayload)
{
    if (!TcpSocket)
    {
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FCaptureProtocolError(TEXT("Invalid TCP socket"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }

	int32 TotalSent = 0;
	while (TotalSent != InPayload.Num())
	{
		int32 Sent = 0;
		bool Result = TcpSocket->Send(InPayload.GetData() + TotalSent, InPayload.Num() - TotalSent, Sent);
		if (!Result)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return FCaptureProtocolError(TEXT("Failed to send the data"));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		TotalSent += Sent;
	}

    return ResultOk;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
TProtocolResult<TArray<uint8>> FTcpClient::ReceiveMessage(const uint64 InSize, const uint32 InWaitTimeoutMs)
{
    if (!TcpSocket)
    {
        return FCaptureProtocolError(TEXT("Invalid TCP socket"));
    }

	TArray<uint8> ReadData;
	ReadData.AddUninitialized(InSize);

	uint32 LeftToRead = InSize;

	while (LeftToRead != 0)
	{
		uint32 PendingSize = 0;
		int32 ReadSize = 0;

		if (!TcpSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(InWaitTimeoutMs)))
		{
			return FCaptureProtocolError(TEXT("Timeout has expired"), TimeoutError);
		}

		if (!TcpSocket->HasPendingData(PendingSize))
		{
			return FCaptureProtocolError(TEXT("Host has been disconnected"), DisconnectedError);
		}

		int32 Size = FMath::Min(LeftToRead, PendingSize);

		const uint32 Offset = InSize - LeftToRead;
		if (!TcpSocket->Recv(ReadData.GetData() + Offset, Size, ReadSize))
		{
			return FCaptureProtocolError(TEXT("Failed to read the data from the TCP socket"));
		}

		if (PendingSize == 0 && ReadSize == 0)
		{
			return FCaptureProtocolError(TEXT("Host has been disconnected"), DisconnectedError);
		}

		LeftToRead -= ReadSize;
	}

    return ReadData;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS


FTcpClientReader::FTcpClientReader(FTcpClient& InClient)
    : Client(InClient)
{
}

TProtocolResult<TArray<uint8>> FTcpClientReader::ReceiveMessage(const uint64 InSize, const uint32 InWaitTimeoutMs)
{
    return Client.ReceiveMessage(InSize, InWaitTimeoutMs);
}


FTcpClientWriter::FTcpClientWriter(FTcpClient& InClient)
    : Client(InClient)
{
}

TProtocolResult<void> FTcpClientWriter::SendMessage(const TArray<uint8>& InPayload)
{
    return Client.SendMessage(InPayload);
}
