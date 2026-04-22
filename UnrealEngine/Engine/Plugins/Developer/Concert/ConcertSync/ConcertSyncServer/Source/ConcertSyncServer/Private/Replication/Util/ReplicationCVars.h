// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"

namespace UE::ConcertSyncServer::Replication
{
/** Whether to log requests and responses to streams on the server. */
extern TAutoConsoleVariable<bool> CVarLogStreamRequestsAndResponsesOnServer;
	
/** Whether to log requests and responses to authority on the server. */
extern TAutoConsoleVariable<bool> CVarLogAuthorityRequestsAndResponsesOnServer;
}
