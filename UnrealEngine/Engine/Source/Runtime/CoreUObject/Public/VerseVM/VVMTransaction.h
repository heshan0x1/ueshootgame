// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "AutoRTFM.h"
#include "Containers/Array.h"
#include "Templates/TypeHash.h"
#include "VVMCell.h"
#include "VVMContext.h"
#include "VVMLog.h"
#include "VVMPlaceholder.h"
#include "VVMPtrVariant.h"
#include "VVMVerse.h"
#include "VVMWriteBarrier.h"

namespace Verse
{
template <typename T>
struct FWriteLog
{
	FWriteLog(const FWriteLog&) = delete;

	FWriteLog(FWriteLog&&) = delete;

	FWriteLog& operator=(const FWriteLog&) = delete;

	FWriteLog& operator=(FWriteLog&&) = delete;

	~FWriteLog() = default;

	FWriteLog()
	{
		checkSlow(IsInline());
		std::memset(Table, 0, InitialCapacity * sizeof(uint64));
	}

	void Append(FAllocationContext Context, FWriteLog& Child)
	{
		for (uint32 I = 0, Last = Child.Num; I != Last; ++I)
		{
			AddImpl(Context, Child.Log[I]);
		}
	}

	void Backtrack(FAccessContext Context)
	{
		for (uint32 I = 0, Last = Num; I != Last; ++I)
		{
			Log[I].Backtrack(Context);
		}
	}

	void Empty()
	{
		if (IsInline())
		{
			EmptyInlineHashTable();
		}
		else
		{
			EmptyHashTable();
		}
	}

protected:
	void AddImpl(FAllocationContext Context, T Entry)
	{
		checkSlow(Entry.Key() != 0);
		if (IsInline())
		{
			AddToInlineHashTable(Context, Entry);
		}
		else
		{
			AddToHashTable(Context, Entry);
		}
	}

private:
	V_FORCEINLINE bool IsInline() { return Table == InlineTable; }

	V_FORCEINLINE bool ShouldGrowTable()
	{
		return 2 * Num > TableCapacity;
	}

	static uint64& FindBucket(uint64 Entry, uint64* Table, uint32 Capacity, bool& bIsNewEntry)
	{
		checkSlow(Capacity && (Capacity & (Capacity - 1)) == 0);
		// We use a simple linear probing hash table.
		uint32 Mask = Capacity - 1;
		uint32 Index = ::GetTypeHash(Entry) & Mask;
		for (;;)
		{
			if (!Table[Index] || Table[Index] == Entry)
			{
				bIsNewEntry = !Table[Index];
				return Table[Index];
			}
			Index = (Index + 1) & Mask;
		}
	}

	FORCENOINLINE void GrowTable(FAllocationContext Context)
	{
		uint32 NewCapacity = TableCapacity * 2;
		if (TableCapacity == InitialCapacity)
		{
			NewCapacity *= 2;
		}

		std::size_t AllocationSize = sizeof(uint64) * NewCapacity;
		uint64* NewTable = BitCast<uint64*>(Context.AllocateAuxCell(AllocationSize));
		std::memset(NewTable, 0, AllocationSize);

		for (uint32 I = 0, Last = Num; I != Last; ++I)
		{
			bool bIsNewEntry;
			FindBucket(Log[I].Key(), NewTable, NewCapacity, bIsNewEntry) = Log[I].Key();
		}

		TableCapacity = NewCapacity;
		Table = NewTable;
	}

	void AddToInlineHashTable(FAllocationContext Context, T Entry)
	{
		for (uint32 I = 0, Last = InitialCapacity; I != Last; ++I)
		{
			if (!Table[I])
			{
				Table[I] = Entry.Key();
				AppendToLog(Context, Entry);
				return;
			}
			if (Entry.Key() == Table[I])
			{
				return;
			}
		}

		GrowTable(Context);
		AddToHashTable(Context, Entry);
	}

	void AddToHashTable(FAllocationContext Context, T Entry)
	{
		bool bIsNewEntry;
		uint64& Bucket = FindBucket(Entry.Key(), Table, TableCapacity, bIsNewEntry);
		if (bIsNewEntry)
		{
			Bucket = Entry.Key();
			AppendToLog(Context, Entry);
			if (ShouldGrowTable())
			{
				GrowTable(Context);
			}
		}
	}

	void EmptyInlineHashTable()
	{
		std::memset(Table, 0, InitialCapacity * sizeof(uint64));
		Num = 0;
		TableCapacity = InitialCapacity;
		LogCapacity = InitialCapacity;
	}

	void EmptyHashTable()
	{
		Table = InlineTable;
		Log = BitCast<T*>(static_cast<char*>(InlineLog));
		EmptyInlineHashTable();
	}

	void AppendToLog(FAllocationContext Context, T Entry)
	{
		if (Num == LogCapacity)
		{
			uint32 NewCapacity = LogCapacity * 2;
			T* NewLog = BitCast<T*>(Context.AllocateAuxCell(NewCapacity * sizeof(T)));
			std::memcpy(NewLog, Log, Num * sizeof(T));
			LogCapacity = NewCapacity;
			Log = NewLog;
		}

		new (Log + Num) T{::MoveTemp(Entry)};
		++Num;
	}

private:
	static constexpr uint32 InitialCapacity = 4;

	uint64* Table = InlineTable;
	T* Log = BitCast<T*>(static_cast<char*>(InlineLog));

	uint64 InlineTable[InitialCapacity];
	alignas(alignof(T)) char InlineLog[InitialCapacity * sizeof(T)];

	uint32 Num = 0;
	uint32 TableCapacity = InitialCapacity;
	// TODO: It's conceivable we could make LogCapacity a function of TableCapacity.
	// But we're just doing the simple thing for now.
	uint32 LogCapacity = InitialCapacity;
};

struct FTrailLogEntry
{
	explicit FTrailLogEntry(VPlaceholder::EMode& Mode)
		: Ptr{&Mode}
		, Value{static_cast<uint64>(Mode)}
		, bMode{true}
	{
	}

	explicit FTrailLogEntry(TWriteBarrier<VValue>& InValue)
		: Ptr{&InValue}
		, Value{InValue.Get().GetEncodedBits()}
		, bMode{false}
	{
	}

	uint64 Key() const
	{
		return reinterpret_cast<uint64>(Ptr);
	}

	void Backtrack(FAccessContext Context)
	{
		if (bMode)
		{
			*static_cast<EMode*>(Ptr) = static_cast<EMode>(Value);
		}
		else
		{
			static_cast<TWriteBarrier<VValue>*>(Ptr)->Set(Context, VValue::Decode(Value));
		}
	}

private:
	using EMode = VPlaceholder::EMode;

	void* Ptr; // VPlaceholder::Mode() is byte-aligned, leaving no bits for TPtrVariant.
	uint64 Value;
	bool bMode;
};

struct FTrailLog : FWriteLog<FTrailLogEntry>
{
	void Add(FAllocationContext Context, VPlaceholder::EMode& Mode)
	{
		AddImpl(Context, FTrailLogEntry{Mode});
	}

	void Add(FAllocationContext Context, TWriteBarrier<VValue>& Value)
	{
		AddImpl(Context, FTrailLogEntry{Value});
	}
};

struct FTrail
{
	FTrail* GetParent() const
	{
		return Parent;
	}

	void Enter(FAllocationContext Context)
	{
		FTrail* CurrentTrail = Context.CurrentTrail();
		V_DIE_IF(CurrentTrail == this);
		V_DIE_IF(Parent);
		Parent = CurrentTrail;
		Context.SetCurrentTrail(this);
	}

	void Exit(FAllocationContext Context)
	{
		FTrail* CurrentTrail = Context.CurrentTrail();
		V_DIE_UNLESS(CurrentTrail == this);
		Context.SetCurrentTrail(Parent);
		if (Parent)
		{
			Parent->Log.Append(Context, Log);
		}
		Log.Empty();
		Parent = nullptr;
	}

	void Abort(FAllocationContext Context)
	{
		FTrail* Next;
		for (FTrail* I = Context.CurrentTrail();; I = Next)
		{
			V_DIE_UNLESS(I);
			I->Log.Backtrack(Context);
			I->Log.Empty();
			Next = I->Parent;
			I->Parent = nullptr;
			if (I == this)
			{
				break;
			}
		}
		Context.SetCurrentTrail(Next);
	}

	void LogBeforeWrite(FAllocationContext Context, VPlaceholder::EMode& Mode)
	{
		Log.Add(Context, Mode);
	}

	void LogBeforeWrite(FAllocationContext Context, TWriteBarrier<VValue>& Value)
	{
		Log.Add(Context, Value);
	}

private:
	FTrailLog Log;
	FTrail* Parent{nullptr};
};

struct FTransactionLogEntry
{
	uintptr_t Key() { return Slot.RawPtr(); }

	using FSlot = TPtrVariant<TWriteBarrier<VValue>*, TWriteBarrier<TAux<void>>*, TWriteBarrier<VCell>*>;

	FSlot Slot;      // The memory location we write OldValue into on abort.
	uint64 OldValue; // VValue or TAux<void> depending on how Slot is encoded.
	static_assert(sizeof(OldValue) == sizeof(VValue));
	static_assert(sizeof(OldValue) == sizeof(VCell*));
	static_assert(sizeof(OldValue) == sizeof(TAux<void>));

	FTransactionLogEntry(TWriteBarrier<VValue>& InSlot, VValue OldValue)
		: Slot(&InSlot)
		, OldValue(OldValue.GetEncodedBits())
	{
	}

	template <typename T, typename = std::enable_if_t<std::is_convertible_v<T*, VCell*>>>
	FTransactionLogEntry(TWriteBarrier<T>& InSlot, T* OldValue)
		: Slot(reinterpret_cast<TWriteBarrier<VCell>*>(&InSlot))
		, OldValue(BitCast<uint64>(OldValue))
	{
	}

	FTransactionLogEntry(TWriteBarrier<TAux<void>>& InSlot, TAux<void> OldValue)
		: Slot(&InSlot)
		, OldValue(BitCast<uint64>(OldValue.GetPtr()))
	{
	}

	void Backtrack(FAccessContext Context)
	{
		if (Slot.Is<TWriteBarrier<VValue>*>())
		{
			TWriteBarrier<VValue>* ValueSlot = Slot.As<TWriteBarrier<VValue>*>();
			ValueSlot->Set(Context, VValue::Decode(OldValue));
		}
		else if (Slot.Is<TWriteBarrier<TAux<void>>*>())
		{
			TWriteBarrier<TAux<void>>* AuxSlot = Slot.As<TWriteBarrier<TAux<void>>*>();
			AuxSlot->Set(Context, TAux<void>(BitCast<void*>(OldValue)));
		}
		else
		{
			TWriteBarrier<VCell>* ValueSlot = Slot.As<TWriteBarrier<VCell>*>();
			ValueSlot->Set(Context, BitCast<VCell*>(OldValue));
		}
	}
};

struct FTransactionLog : FWriteLog<FTransactionLogEntry>
{
	template <typename T>
	void Add(FAllocationContext Context, TWriteBarrier<T>& Slot)
	{
		AddImpl(Context, FTransactionLogEntry{Slot, Slot.Get()});
	}
};

#if UE_BUILD_DEBUG
#define VERSE_TRANSACTION_HAS_AUTORTFM_ID 1
#else
#define VERSE_TRANSACTION_HAS_AUTORTFM_ID 0
#endif

struct FTransaction
{
	FTransactionLog Log;
	FTransaction* Parent{nullptr};
	bool bHasStarted{false};
	bool bHasCommitted{false};
	bool bHasAborted{false};
	bool bHasRolledBackAutoRTFMTransaction{false};

#if VERSE_TRANSACTION_HAS_AUTORTFM_ID
	AutoRTFM::TransactionID AutoRTFMTransactionID{0};
#endif

	// Note: We can Abort before we Start because of how leniency works. For example, we can't
	// Start the transaction until the effect token is concrete, but the effect token may become
	// concrete after failure occurs.
	void Start(FRunningContext Context)
	{
		AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

		V_DIE_IF(bHasCommitted);
		V_DIE_IF(bHasStarted);
		V_DIE_IF(Parent);
		bHasStarted = true;

		if (!bHasAborted)
		{
			AutoRTFM::ForTheRuntime::StartTransaction();
			Parent = Context.CurrentTransaction();
			Context.SetCurrentTransaction(this);
#if VERSE_TRANSACTION_HAS_AUTORTFM_ID
			AutoRTFMTransactionID = AutoRTFM::CurrentTransactionID();
#endif
		}
	}

	// We can't call Commit before we Start because we serialize Start then Commit via the effect token.
	void Commit(FRunningContext Context)
	{
		AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

		V_DIE_UNLESS(bHasStarted);
		V_DIE_IF(bHasAborted);
		V_DIE_IF(bHasCommitted);
		bHasCommitted = true;
#if VERSE_TRANSACTION_HAS_AUTORTFM_ID
		V_DIE_UNLESS(AutoRTFMTransactionID == AutoRTFM::CurrentTransactionID());
		AutoRTFMTransactionID = 0;
#endif
		AutoRTFM::ForTheRuntime::CommitTransaction();
		if (Parent)
		{
			Parent->Log.Append(Context, Log);
		}
		Context.SetCurrentTransaction(Parent);
	}

	// See above comment as to why we might Abort before we start.
	void Abort(FRunningContext Context)
	{
		AutoRTFM::UnreachableIfClosed("#jira SOL-8415");

		V_DIE_IF(bHasCommitted);
		V_DIE_IF(bHasAborted);
		bHasAborted = true;
		if (bHasStarted)
		{
			V_DIE_UNLESS(Context.CurrentTransaction() == this);

			if (!bHasRolledBackAutoRTFMTransaction)
			{
#if VERSE_TRANSACTION_HAS_AUTORTFM_ID
				V_DIE_UNLESS(AutoRTFMTransactionID == AutoRTFM::CurrentTransactionID());
				AutoRTFMTransactionID = 0;
#endif
				// Rollback the transaction.
				AutoRTFM::ForTheRuntime::RollbackTransaction();
				// Clear the transaction status so the parent transaction(s)
				// can continue to behave normally.
				AutoRTFM::ForTheRuntime::ClearTransactionStatus();
				bHasRolledBackAutoRTFMTransaction = true;
			}

			Log.Backtrack(Context);
			Context.SetCurrentTransaction(Parent);
		}
		else
		{
			V_DIE_IF(Parent);
		}
	}

	template <typename T>
	void LogBeforeWrite(FAllocationContext Context, TWriteBarrier<T>& Slot)
	{
		Log.Add(Context, Slot);
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
