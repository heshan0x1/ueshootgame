// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMRef.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMRefInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/Inline/VVMWriteBarrierInline.h"
#include "VerseVM/VVMBytecodesAndCaptures.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/VVMTask.h"
#include "VerseVM/VVMValuePrinting.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VRefRareData);
TGlobalTrivialEmergentTypePtr<&VRefRareData::StaticCppClassInfo> VRefRareData::EmergentType;

COREUOBJECT_API extern FOpErr StopInterpreterSentry;

template <typename TVisitor>
void VRefRareData::VisitReferencesImpl(TVisitor& Visitor)
{
	if (LiveTask)
	{
		if (LiveTask->ResumePC == &StopInterpreterSentry)
		{
			LiveTask.Reset();
		}
		else
		{
			Visitor.Visit(LiveTask, TEXT("LiveTask"));
		}
	}
	FCellUniqueLock Lock(Mutex);
	for (auto I = AwaiterBuffer.CreateIterator(); I; ++I)
	{
		if (ContainsAwaitTask(*I->Task, *I->AwaitPC))
		{
			Visitor.Visit(I->Task, TEXT("Tasks"));
		}
		else
		{
			I->Detach(AwaiterBuffer, AwaiterHeader);
			I.RemoveCurrent();
		}
	}
	Visitor.ReportNativeBytes(GetAllocatedSize());
}

void VRefRareData::SetLiveTask(FAllocationContext Context, VTask* Task)
{
	LiveTask.SetTransactionally(Context, Task);
}

VTask* VRefRareData::GetLiveTask() const
{
	return LiveTask.Get();
}

void VRefRareData::AddAwaitTask(FAccessContext Context, VTask& Task, const FOp& AwaitPC)
{
	FCellUniqueLock Lock(Mutex);
	TNativeAllocationGuard NativeAllocationGuard{this};
	FRefAwaiter Key{Context, Task, AwaitPC};
	FSetElementId ElementId;
	FRefAwaiter* Awaiter;
	if (ElementId = AwaiterBuffer.FindId(Key); ElementId.IsValidId())
	{
		Awaiter = &AwaiterBuffer[ElementId];
		Awaiter->Detach(AwaiterBuffer, AwaiterHeader);
	}
	else
	{
		ElementId = AwaiterBuffer.Emplace(::MoveTemp(Key));
		Awaiter = &AwaiterBuffer[ElementId];
	}
	Awaiter->Attach(AwaiterBuffer, AwaiterHeader, ElementId);
}

bool VRefRareData::ContainsAwaitTask(const VTask& Task, const FOp& AwaitPC)
{
	if (Task.bRunning)
	{
		return false;
	}
	// Rely on transactional `ResumePC` updates to know when this map
	// entry's addition should have been rolled back or when this map
	// entry's task has moved passed the `await`.
	if (Task.ResumePC != &AwaitPC)
	{
		return false;
	}
	return true;
}

DEFINE_DERIVED_VCPPCLASSINFO(VRef);
TGlobalTrivialEmergentTypePtr<&VRef::StaticCppClassInfo> VRef::GlobalTrivialEmergentType;

template <typename TVisitor>
void VRef::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Value, TEXT("Value"));
	Visitor.Visit(RareData, TEXT("RareData"));
}

void VRef::AddAwaitTask(FAllocationContext Context, VTask& AwaitTask, const FOp& AwaitPC)
{
	if (!RareData)
	{
		RareData.Set(Context, VRefRareData::New(Context));
	}
	RareData->AddAwaitTask(Context, AwaitTask, AwaitPC);
}

void VRef::AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth)
{
	Get(Context).AppendToString(Context, Builder, Format, VisitedObjects, RecursionDepth + 1);
}

TSharedPtr<FJsonValue> VRef::ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs)
{
	return Get(Context).ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
}

void VRef::SerializeLayout(FAllocationContext Context, VRef*& This, FStructuredArchiveVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		This = &VRef::New(Context);
	}
}

void VRef::VisitMembersImpl(FAllocationContext Context, FDebuggerVisitor& Visitor)
{
	if (Value.IsRoot())
	{
		return;
	}
	VCell* Cell = Value.Get(Context).ExtractCell();
	if (!Cell)
	{
		return;
	}
	Cell->VisitMembers(Context, Visitor);
}

void VRef::SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor)
{
	Visitor.Visit(Value, TEXT("Value"));
	Visitor.Visit(RareData, TEXT("RareData"));
}

VTask* VRef::GetLiveTask() const
{
	return RareData ? RareData->GetLiveTask() : nullptr;
}

void VRef::SetLiveTask(FAllocationContext Context, VTask* Task)
{
	if (!RareData)
	{
		RareData.Set(Context, VRefRareData::New(Context));
	}
	RareData->SetLiveTask(Context, Task);
}
} // namespace Verse

#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
