// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Templates/UniquePtr.h"
#include "UdpMessageProcessor.h"

struct FUdpMessageLoggingImpl;

struct FUdpMessageProcessorLogger
{
	FUdpMessageProcessorLogger(FIPv4Endpoint InEndpoint, uint16 PortNo, FUdpMessageProcessor* InProcessor);
	~FUdpMessageProcessorLogger();

	/** Start enhanced logging for UdpMessageProcessor */
	void StartLogging();

	/** Stop enhanced logging for UdpMessageProcessor */
	void StopLogging();

	/** Report the current logging state. */
	bool IsLogging() const;

	/** Dump a report of the current known nodes from the system. */
	void DumpKnownNodeInfo();
	
private:
	FIPv4Endpoint Endpoint;
		
	FUdpMessageProcessor* Processor;
	TUniquePtr<FUdpMessageLoggingImpl> Impl;
};


