// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicUAF.h"

DEFINE_LOG_CATEGORY(LogRigLogicUAF);

namespace UE::UAF
{
    void FRigLogicModule::StartupModule()
    {
    }

    void FRigLogicModule::ShutdownModule()
    {
    }
} // namespace UE::UAF

IMPLEMENT_MODULE(UE::UAF::FRigLogicModule, RigLogicUAF)