// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "Helpers/OnlineHelpers.h"

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"

namespace UE::TestCommon {

void Tick()
{
	double TickTime = FTimespan::FromMilliseconds(TickFrequencyMs).GetTotalSeconds();
	FTSTicker::GetCoreTicker().Tick(TickTime);
	FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
}

/* UE::TestCommon */ }