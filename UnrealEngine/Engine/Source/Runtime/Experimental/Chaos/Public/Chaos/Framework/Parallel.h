// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Function.h"

namespace Chaos
{
	void CHAOS_API PhysicsParallelForRange(int32 InNum, TFunctionRef<void(int32, int32)> InCallable, const int32 MinBatchSize, bool bForceSingleThreaded = false);
	void CHAOS_API PhysicsParallelFor(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded = false);
	void CHAOS_API InnerPhysicsParallelForRange(int32 InNum, TFunctionRef<void(int32, int32)> InCallable, const int32 MinBatchSize, bool bForceSingleThreaded = false);
	void CHAOS_API InnerPhysicsParallelFor(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded = false);
	void CHAOS_API PhysicsParallelForWithContext(int32 InNum, TFunctionRef<int32 (int32, int32)> InContextCreator, TFunctionRef<void(int32, int32)> InCallable, bool bForceSingleThreaded = false);
	//void CHAOS_API PhysicsParallelFor_RecursiveDivide(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded = false);

	/**
	 * Helper to call from task execution logic that does not use ParallelFor, to know if it should use parallel execution or not.
	 * Returns true if the tasks should be run in parallel based on the current context.
	 * @param InTaskNum The total number of tasks that need to run, will be compared to MinParallelTaskSize
	 */
	bool CHAOS_API ShouldExecuteTasksInParallel(int32 InTaskNum);

	CHAOS_API extern int32 MaxNumWorkers;
	CHAOS_API extern int32 SmallBatchSize;
	CHAOS_API extern int32 LargeBatchSize;
	CHAOS_API extern int32 MinParallelTaskSize;
	CHAOS_API extern bool bDisablePhysicsParallelFor;
	CHAOS_API extern bool bDisableParticleParallelFor;
	CHAOS_API extern bool bDisableCollisionParallelFor;
}