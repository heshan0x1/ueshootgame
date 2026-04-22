// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMType.h"

namespace Verse
{
enum class EValueStringFormat;
struct VTask;
struct FRefAwaiter;

struct FRefAwaiterHeader
{
	FRefAwaiterHeader() = default;

	FSetElementId Prev;
	FSetElementId Next;

	template <typename FunctionType>
	bool AnyOf(const TSet<FRefAwaiter>&, FunctionType) const;
};

struct FRefAwaiter : FRefAwaiterHeader
{
	FRefAwaiter(FAccessContext Context, VTask& Task, const FOp& AwaitPC)
		: Task{Context, Task}
		, AwaitPC{&AwaitPC}
	{
	}

	TWriteBarrier<VTask> Task;
	const FOp* AwaitPC;

	friend bool operator==(const FRefAwaiter&, const FRefAwaiter&);

	friend uint32 GetTypeHash(const FRefAwaiter&);

	void Attach(TSet<FRefAwaiter>& Set, FRefAwaiterHeader& Header, FSetElementId ThisElementId)
	{
		Prev = Header.Prev;
		Get(Set, Header, Prev).Next = ThisElementId;
		Next = {};
		Get(Set, Header, Next).Prev = ThisElementId;
	}

	void Detach(TSet<FRefAwaiter>& Set, FRefAwaiterHeader& Header)
	{
		Get(Set, Header, Prev).Next = Next;
		Get(Set, Header, Next).Prev = Prev;
	}

private:
	static FRefAwaiterHeader& Get(TSet<FRefAwaiter>& Set, FRefAwaiterHeader& Header, FSetElementId ElementId)
	{
		if (ElementId.IsValidId())
		{
			return Set[ElementId];
		}
		return Header;
	}
};

struct VRefRareData : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> EmergentType;

	static VRefRareData& New(FAllocationContext Context)
	{
		return *new (Context.Allocate(FHeap::DestructorSpace, sizeof(VRefRareData))) VRefRareData{Context};
	}

	VTask* GetLiveTask() const;

	void SetLiveTask(FAllocationContext, VTask*);

	void AddAwaitTask(FAccessContext, VTask&, const FOp&);

	template <typename UnaryFunction>
	bool AnyAwaitTask(UnaryFunction) const;

	size_t GetAllocatedSize() const
	{
		return AwaiterBuffer.GetAllocatedSize();
	}

private:
	explicit VRefRareData(FAllocationContext Context)
		: VCell{Context, &EmergentType.Get(Context)}
	{
		FHeap::ReportAllocatedNativeBytes(GetAllocatedSize());
	}

	VRefRareData(const VRefRareData&) = delete;

	VRefRareData(VRefRareData&&) = delete;

	VRefRareData& operator=(const VRefRareData&) = delete;

	VRefRareData& operator=(VRefRareData&&) = delete;

	~VRefRareData()
	{
		FHeap::ReportDeallocatedNativeBytes(GetAllocatedSize());
	}

	static bool ContainsAwaitTask(const VTask&, const FOp&);

	TWriteBarrier<VTask> LiveTask;
	TSet<FRefAwaiter> AwaiterBuffer;
	FRefAwaiterHeader AwaiterHeader;
};

struct VRef : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VRef& New(FAllocationContext Context)
	{
		return *new (Context.AllocateFastCell(sizeof(VRef))) VRef(Context);
	}

	VValue Get(FAllocationContext Context)
	{
		return Value.Get(Context);
	}

	void Set(FAllocationContext Context, VValue NewValue);

	void SetNonTransactionally(FAccessContext Context, VValue NewValue)
	{
		return Value.Set(Context, NewValue);
	}

	void AppendToStringImpl(FAllocationContext Context, FUtf8StringBuilderBase& Builder, EValueStringFormat Format, TSet<const void*>& VisitedObjects, uint32 RecursionDepth);
	TSharedPtr<FJsonValue> ToJSONImpl(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs);
	static void SerializeLayout(FAllocationContext Context, VRef*& This, FStructuredArchiveVisitor& Visitor);
	void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

	VTask* GetLiveTask() const;

	void SetLiveTask(FAllocationContext, VTask*);

	void AddAwaitTask(FAllocationContext, VTask&, const FOp&);

	template <typename UnaryFunction>
	bool AnyAwaitTask(UnaryFunction F) const
	{
		return RareData && RareData->AnyAwaitTask(F);
	}

	void VisitMembersImpl(FAllocationContext, FDebuggerVisitor&);

private:
	VRestValue Value;
	TWriteBarrier<VRefRareData> RareData;

	explicit VRef(FAllocationContext Context)
		: VHeapValue{Context, &GlobalTrivialEmergentType.Get(Context)}
		// TODO: Figure out what split depth meets here.
		, Value{0}
	{
	}
};

template <typename SetType>
VValue UnwrapTransparentRef(
	FAllocationContext Context,
	VValue Value,
	VTask* Task,
	FOp* AwaitPC,
	SetType Set)
{
	AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

	if (VRef* TransparentRef = Value.ExtractTransparentRef())
	{
		if (AwaitPC)
		{
			TransparentRef->AddAwaitTask(Context, *Task, *AwaitPC);
		}
		return TransparentRef->Get(Context);
	}
	if (AwaitPC)
	{
		VRef& NewRef = VRef::New(Context);
		NewRef.SetNonTransactionally(Context, Value);
		NewRef.AddAwaitTask(Context, *Task, *AwaitPC);
		Set(VValue::TransparentRef(NewRef));
	}
	return Value;
}
} // namespace Verse
#endif // WITH_VERSE_VM
