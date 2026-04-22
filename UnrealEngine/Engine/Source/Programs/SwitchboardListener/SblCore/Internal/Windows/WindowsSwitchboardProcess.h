// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SwitchboardProcess.h"


/** Windows implementation of FSwitchboardProcess. See FGenericSwitchboardProcess for details. */
struct FWindowsSwitchboardProcess : public FGenericSwitchboardProcess
{
public:
	using FGenericSwitchboardProcess::CreateProc;

	static FCreateProcResult CreateProc(const FCreateProcParams& InParams);
};


using FSwitchboardProcess = FWindowsSwitchboardProcess;
