// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"
#include "Misc/ScopeLock.h"

#include <atomic>

namespace UE::LinearBlockAllocator::Private
{

struct FBlock
{
	int32 NextOffset;
};

}
namespace UE { class FLinearBlockAllocatorThreadAccessor; }

namespace UE
{

/**
 * Provides Malloc operation that serves memory from blocks, and does not free up memory until
 * the allocator goes out of scope. This is an optimization to reduce the cost of Malloc and Free
 * for dynamically allocated scratch or intermediate data.
 * 
 * The expected use case is to provide string allocations for FNames.
 *
 * Memory for the blocks is allocated from FMemory::Malloc. Large allocations will cause wasted
 * memory as the excess memory from the previous block goes unused.
 * 
 * When used directly, the allocator is not threadsafe. A high-performance threadsafe adaptor
 * is available by using FLinearBlockAllocatorThreadAccessor, but in that case every thread using
 * the allocator must access it only through FLinearBlockAllocatorThreadAccessor.
 * 
 */
class FLinearBlockAllocator
{
public:
	~FLinearBlockAllocator();

	void* Malloc(SIZE_T Size, uint32 Alignment);

private:
	void* TryAllocateFromBlock(SIZE_T Size, uint32 Alignment, UE::LinearBlockAllocator::Private::FBlock* Block);
	bool RequiresCustomAllocation(SIZE_T Size, uint32 Alignment) const;
	UE::LinearBlockAllocator::Private::FBlock* GetNewBlock();
	void* GetCustomAllocation(SIZE_T Size, uint32 Alignment);

	TArray<void*> BlockAllocs; // guarded by LockForThreadSafeAllocator when threading
	UE::LinearBlockAllocator::Private::FBlock* LastBlock = nullptr; // unused during threading
	uint32 BlockAlignment = 4; // constant during threading
	SIZE_T BlockSize = 1 << 16; // constant during threading
	FCriticalSection LockForThreadSafeAllocator;
	std::atomic<int32> ReferenceCount{ 0 };

	friend class UE::FLinearBlockAllocatorThreadAccessor;
};

/**
 * Accessor for a LinearBlockAllocator when multithreading. Every thread using the Allocator
 * must access it through its own FLinearBlockAllocatorThreadAccessor.
 * 
 * Intended use is to share an allocator amongst the task threads used within a single function
 * in e.g. ParallelFor.
 * 
 * The lifetime of FLinearBlockAllocatorThreadAccessor must be within the lifetime of the
 * Accessor, an assertion fires if FLinearBlockAllocatorThreadAccessor are still allocated
 * when the Allocator is destructed. So the thread that destroys the allocator must wait
 * on all other threads using the Allocator to destroy their FLinearBlockAllocatorThreadAccessor.
 */
class FLinearBlockAllocatorThreadAccessor
{
public:
	explicit FLinearBlockAllocatorThreadAccessor(FLinearBlockAllocator& InAllocator);
	~FLinearBlockAllocatorThreadAccessor();

	void* Malloc(SIZE_T Size, uint32 Alignment);

private:
	FLinearBlockAllocator& Allocator;
	UE::LinearBlockAllocator::Private::FBlock* LastBlock = nullptr;
};

} // namespace UE


///////////////////////////////////////////////////////
// Inline implementations
///////////////////////////////////////////////////////


namespace UE
{

inline FLinearBlockAllocator::~FLinearBlockAllocator()
{
	using namespace UE::LinearBlockAllocator::Private;

	checkf(ReferenceCount.load(std::memory_order_relaxed) == 0,
		TEXT("FLinearBlockAllocator is destroyed while still in use by a FLinearBlockAllocatorThreadAccessor."));
	for (void* BlockAlloc : BlockAllocs)
	{
		FMemory::Free(BlockAlloc);
	}
}

inline void* FLinearBlockAllocator::Malloc(SIZE_T Size, uint32 Alignment)
{
	checkf(ReferenceCount.load(std::memory_order_relaxed) == 0,
		TEXT("FLinearBlockAllocator::Malloc is being used directly while in use by a FLinearBlockAllocatorThreadAccessor, this is not allowed, all malloc calls must go through a FLinearBlockAllocatorThreadAccessor."));

	void* Result = TryAllocateFromBlock(Size, Alignment, LastBlock);
	if (Result)
	{
		return Result;
	}

	if (RequiresCustomAllocation(Size, Alignment))
	{
		return GetCustomAllocation(Size, Alignment);
	}

	LastBlock = GetNewBlock();
	Result = TryAllocateFromBlock(Size, Alignment, LastBlock);
	check(Result);
	return Result;
}

inline void* FLinearBlockAllocator::TryAllocateFromBlock(SIZE_T Size, uint32 Alignment,
	UE::LinearBlockAllocator::Private::FBlock* Block)
{
	if (!Block)
	{
		return nullptr;
	}

	void* NextPtr = (void*)(((SIZE_T)Block) + ((SIZE_T)Block->NextOffset));
	void* AlignedPtr = Align(NextPtr, Alignment);
	SIZE_T NewOffset = ((SIZE_T)AlignedPtr) + Size - ((SIZE_T)Block);
	if (NewOffset > BlockSize)
	{
		return nullptr;
	}

	Block->NextOffset = (int32)NewOffset;
	return AlignedPtr;
}

bool FLinearBlockAllocator::RequiresCustomAllocation(SIZE_T Size, uint32 Alignment) const
{
	using namespace UE::LinearBlockAllocator::Private;

	// Compute the worse case AlignmentPadding, which occurs if the pointer we received for the block allocation
	// is aligned to BlockAlignment and nothing higher, which we can calculate by assuming the pointer starts at
	// 1*BlockAlignment. The starting position is then BlockHeaderSize after that, and we have to AlignUp the
	// starting position.
	constexpr uint32 BlockHeaderSize = sizeof(FBlock);
	uint32 StartingOffset = BlockAlignment + BlockHeaderSize;
	uint32 AlignedOffset = Align(StartingOffset, Alignment);
	uint32 AlignmentPadding = AlignedOffset - StartingOffset;
	SIZE_T RequiredBlockSize = Size + AlignmentPadding + BlockHeaderSize;
	return RequiredBlockSize > BlockSize;
}

UE::LinearBlockAllocator::Private::FBlock* FLinearBlockAllocator::GetNewBlock()
{
	using namespace UE::LinearBlockAllocator::Private;

	constexpr uint32 BlockHeaderSize = sizeof(FBlock);
	void* BlockAllocation = FMemory::Malloc(BlockSize, BlockAlignment);
	FBlock* NewBlock = reinterpret_cast<FBlock*>(BlockAllocation);
	NewBlock->NextOffset = BlockHeaderSize;
	BlockAllocs.Add(BlockAllocation);
	return NewBlock;
}

void* FLinearBlockAllocator::GetCustomAllocation(SIZE_T Size, uint32 Alignment)
{
	using namespace UE::LinearBlockAllocator::Private;

	void* CustomAllocation = FMemory::Malloc(Size, Alignment);
	BlockAllocs.Add(CustomAllocation);
	return CustomAllocation;
}

inline FLinearBlockAllocatorThreadAccessor::FLinearBlockAllocatorThreadAccessor(FLinearBlockAllocator& InAllocator)
	: Allocator(InAllocator)
{
	Allocator.ReferenceCount.fetch_add(1, std::memory_order_relaxed);
}

inline FLinearBlockAllocatorThreadAccessor::~FLinearBlockAllocatorThreadAccessor()
{
	Allocator.ReferenceCount.fetch_add(-1, std::memory_order_relaxed);
}

inline void* FLinearBlockAllocatorThreadAccessor::Malloc(SIZE_T Size, uint32 Alignment)
{
	void* Result = Allocator.TryAllocateFromBlock(Size, Alignment, LastBlock);
	if (Result)
	{
		return Result;
	}

	{
		FScopeLock ScopeLockForThreadSafeAllocator(&Allocator.LockForThreadSafeAllocator);
		if (Allocator.RequiresCustomAllocation(Size, Alignment))
		{
			return Allocator.GetCustomAllocation(Size, Alignment);
		}
		LastBlock = Allocator.GetNewBlock();
	}
	Result = Allocator.TryAllocateFromBlock(Size, Alignment, LastBlock);
	check(Result);
	return Result;
}

} // namespace UE