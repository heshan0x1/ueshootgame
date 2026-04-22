// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Communication/TcpReaderWriter.h"

class FDataProvider : public ITcpSocketReader
{
public:

	FDataProvider(TArray<uint8> InData)
		: Data(MoveTemp(InData))
	{
	}

	virtual TProtocolResult<TArray<uint8>> ReceiveMessage(const uint64 InSize, const uint32 InMaxWaitTimeMs = DefaultWaitTimeoutMs) override
	{
		if (InSize > Data.Num())
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return FCaptureProtocolError(TEXT("Failed to receive data"));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		TArray<uint8> DataToBeReturned(Data.GetData(), InSize);

		Data.RemoveAt(0, InSize);

		return DataToBeReturned;
	}

private:

	TArray<uint8> Data;
};

class FDataSender : public ITcpSocketWriter
{
public:

	FDataSender()
	{
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual TProtocolResult<void> SendMessage(const TArray<uint8>& InPayload) override
	{
		Data.Append(InPayload.GetData(), InPayload.Num());

		return ResultOk;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	const TArray<uint8>& GetData() const
	{
		return Data;
	}

private:

	TArray<uint8> Data;
};

class FFailedDataSender : public FDataSender
{
public:

	virtual TProtocolResult<void> SendMessage(const TArray<uint8>& InPayload) override
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return FCaptureProtocolError(TEXT("Failed to send the data"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
};