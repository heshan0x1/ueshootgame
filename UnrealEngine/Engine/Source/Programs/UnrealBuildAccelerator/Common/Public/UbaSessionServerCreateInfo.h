// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaSessionCreateInfo.h"

namespace uba
{
	class NetworkServer;

	struct SessionServerCreateInfo : SessionCreateInfo
	{
		SessionServerCreateInfo(Storage& s, NetworkServer& c, LogWriter& writer = g_consoleLogWriter) : SessionCreateInfo(s, writer), server(c) {}

		void Apply(const Config& config);

		NetworkServer& server;
		bool resetCas = false;
		bool remoteExecutionEnabled = true;
		bool nameToHashTableEnabled = true;
		bool remoteLogEnabled = false; // If Uba is built in debug, then the logs will be sent back to server
		bool remoteTraceEnabled = false; // If this is true, the agents will run trace and send the .uba file back to server
		bool traceIOEnabled = true; // Enable IO information for storage 
	};
}
