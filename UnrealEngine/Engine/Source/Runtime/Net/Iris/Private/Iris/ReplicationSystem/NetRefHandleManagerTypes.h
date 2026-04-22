// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"

namespace UE::Net
{
	struct FReplicationProtocol;
}

namespace UE::Net::Private
{

	/** Holds important parameters needed to create a NetObject */
	struct FCreateNetObjectParams
	{
		FNetObjectFactoryId NetFactoryId = InvalidNetObjectFactoryId;
		EIrisAsyncLoadingPriority IrisAsyncLoadingPriority = EIrisAsyncLoadingPriority::Default;
		const UE::Net::FReplicationProtocol* ReplicationProtocol = nullptr;
	};

}