// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

namespace UE::Mutable::Private::ParallelExecutionUtils
{
    void InvokeBatchParallelFor(int32 Num, TFunctionRef<void(int32)> Body);
    void InvokeBatchParallelForSingleThread(int32 Num, TFunctionRef<void(int32)> Body);
}

