// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMTaskGroup.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VTaskGroup);
DEFINE_TRIVIAL_VISIT_REFERENCES(VTaskGroup);
TGlobalTrivialEmergentTypePtr<&VTaskGroup::StaticCppClassInfo> VTaskGroup::GlobalTrivialEmergentType;

void VTaskGroup::ConductCensusImpl()
{
	FCellUniqueLock Lock(Mutex);

	for (TSet<TWeakBarrier<VTask>>::TIterator Iter = ActiveTasks.CreateIterator(); Iter; ++Iter)
	{
		// If the cell that the VTask is allocated in is not marked (i.e. non-live) during GC marking,
		// the weak reference will be removed; thus, we can remove the task from our group.
		// This can happen if a task is garbage-collected after it starts but before ending naturally.
		if (Iter->ClearWeakDuringCensus())
		{
			Iter.RemoveCurrent();
		}
	}
}

int64 VTaskGroup::GetNumActive() const
{
	FCellUniqueLock Lock(Mutex);

	int64 Num = 0;
	for (TSet<TWeakBarrier<VTask>>::TConstIterator Iter = ActiveTasks.CreateConstIterator(); Iter; ++Iter)
	{
		if (VTask* RootTask = Iter->Get())
		{
			Num += RootTask->GetNumChildrenRecursively() + 1;
		}
	}
	return Num;
}

bool VTaskGroup::HasActiveTasks() const
{
	FCellUniqueLock Lock(Mutex);

	return !ActiveTasks.IsEmpty();
}

bool VTaskGroup::AddTask(FAllocationContext, VTask& Task)
{
	// Only root-level tasks are needed in the task group.
	// Child tasks can be found by walking the task graph.
	if (Task.Parent != nullptr)
	{
		return false;
	}

	FCellUniqueLock Lock(Mutex);

	ActiveTasks.Emplace(InPlace, Task);

	return true;
}

bool VTaskGroup::AddTaskTransactionally(FAllocationContext, VTask& Task)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8465");

	// Only root-level tasks are needed in the task group.
	// Child tasks can be found by walking the task graph.
	if (Task.Parent != nullptr)
	{
		return false;
	}

	FCellUniqueLock Lock(Mutex);

	AutoRTFM::EContextStatus ContextStatus = AutoRTFM::Close([this, &Task] {
		ActiveTasks.Emplace(InPlace, Task);
	});
	V_DIE_UNLESS(ContextStatus == AutoRTFM::EContextStatus::OnTrack);

	return true;
}

bool VTaskGroup::RemoveTask(FAllocationContext, VTask& Task)
{
	FCellUniqueLock Lock(Mutex);

	return ActiveTasks.Remove(TWeakBarrier<VTask>(Task)) > 0;
}

bool VTaskGroup::RemoveTaskTransactionally(FAllocationContext, VTask& Task)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8465");

	FCellUniqueLock Lock(Mutex);

	bool bFoundTask = false;
	AutoRTFM::EContextStatus ContextStatus = AutoRTFM::Close([this, &Task, &bFoundTask] {
		bFoundTask = ActiveTasks.Remove(TWeakBarrier<VTask>(Task)) > 0;
	});
	V_DIE_UNLESS(ContextStatus == AutoRTFM::EContextStatus::OnTrack);

	return bFoundTask;
}

void VTaskGroup::TerminateAll(FAllocationContext Context)
{
	while (VTask* Task = GetAnyTask())
	{
		Task->Terminate(Context);
	}
}

VTask* VTaskGroup::GetAnyTask()
{
	FCellUniqueLock Lock(Mutex);
	TWeakBarrier<VTask>* Task = ActiveTasks.FindArbitraryElement();
	return Task ? Task->Get() : nullptr;
}

} // namespace Verse

#endif // WITH_VERSE_VM
