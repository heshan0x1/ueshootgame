// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "AutoRTFM.h"
#include "Containers/Set.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/VVMTask.h"
#include "VerseVM/VVMWeakBarrier.h"

namespace Verse
{

struct VTaskGroup : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	AUTORTFM_OPEN static VTaskGroup& New(FAllocationContext Context)
	{
		// We use `DestructorAndCensusSpace` because of ActiveTasks. Its elements are weak and must participate in census, and TSet destruction must occur.
		// VTaskGroup::New is safe to call from the closed; if an abort occurs, the allocated VTaskGroup can be cleaned up by garbage collection.
		return *new (Context.Allocate(FHeap::DestructorAndCensusSpace, sizeof(VTaskGroup))) VTaskGroup(Context);
	}

	/** Reports if there are any active tasks in the group; analogous to `GetNumActive() > 0`. */
	AUTORTFM_OPEN COREUOBJECT_API bool HasActiveTasks() const;

	/** Reports the number of tasks in the group. The exact number is considered an implementation detail and is meant for use in unit tests. */
	AUTORTFM_OPEN COREUOBJECT_API int64 GetNumActive() const;

	/** If the passed-in VTask is top-level, adds it to the task group and returns true; returns false otherwise. */
	AUTORTFM_DISABLE COREUOBJECT_API bool AddTask(FAllocationContext, VTask& Task);

	/** Removes a VTask from the group. Does not terminate or otherwise affect the task itself. Returns true if the VTask was contained in the group. */
	AUTORTFM_DISABLE COREUOBJECT_API bool RemoveTask(FAllocationContext, VTask& Task);

	/** If the passed-in VTask is top-level, adds it to the task group (transactionally) and returns true; returns false otherwise. */
	/*AUTORTFM_DISABLE*/ COREUOBJECT_API bool AddTaskTransactionally(FAllocationContext, VTask& Task);

	/** Removes a VTask from the group. Supports rollback. Does not terminate or otherwise affect the task itself. Returns true if the VTask was contained in the group. */
	/*AUTORTFM_DISABLE*/ COREUOBJECT_API bool RemoveTaskTransactionally(FAllocationContext, VTask& Task);

	/** Terminates every VTask in the group. This is not safe to do when VTasks are running; only terminate at the top level, or during unwinding. */
	AUTORTFM_DISABLE COREUOBJECT_API void TerminateAll(FAllocationContext Context);

private:
	AUTORTFM_OPEN VTaskGroup(FAllocationContext Context)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
	{
	}

	AUTORTFM_DISABLE VTask* GetAnyTask();

	AUTORTFM_DISABLE void ConductCensusImpl();

	TSet<TWeakBarrier<VTask>> ActiveTasks;
};

} // namespace Verse

#endif // WITH_VERSE_VM
