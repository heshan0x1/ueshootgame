// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{

// Static bit-map that uses VInt as its storage (Bits are 0-indexed)
// Thus, its max size is currently `VHeapInt::MaxLength`
struct VBitMap
{
	static const uint32 NumDigitBits = 32;

	TWriteBarrier<VInt> Storage;

	// one must call init after default construction before using other functionality otherwise one is likely to crash
	VBitMap() = default;

	VBitMap(FAllocationContext Context, uint32 InSize, bool bSet = false) { Init(Context, InSize, bSet); }
	VBitMap(FAllocationContext Context, const VBitMap& Other)
	{
		if (VHeapInt* HeapInt = Other.Storage.Get().DynamicCast<VHeapInt>())
		{
			Storage.Set(Context, VInt(VValue(*VHeapInt::Copy(Context, *HeapInt))));
		}
		else
		{
			Storage = Other.Storage;
		}
	}

	void Init(FAllocationContext Context, uint32 InSize, bool bSet = false)
	{
		if (InSize < NumDigitBits)
		{
			int32 Int32 = 0;
			if (bSet)
			{
				Int32 = (1 << InSize) - 1;
			}
			Storage.Set(Context, VInt(Int32));
		}
		else
		{
			uint64 Size = FMath::DivideAndRoundUp<uint64>(InSize, NumDigitBits);
			VHeapInt* HeapInt = VHeapInt::CreateWithLength(Context, Size);
			HeapInt->Initialize(VHeapInt::InitializationType::WithZero);
			if (bSet)
			{
				for (uint32 Index = 0; Index < HeapInt->GetLength() - 1; ++Index)
				{
					HeapInt->SetDigit(Index, UINT32_MAX);
				}
				HeapInt->SetDigit(HeapInt->GetLength() - 1, InSize % NumDigitBits == 0 ? UINT32_MAX : (1 << InSize % NumDigitBits) - 1);
			}
			Storage.Set(Context, VInt(VValue(*HeapInt))); // We need to construct this way to avoid having the VInt ctor convert the heap int into a plain VValue
		}
	}

	bool Equals(FAllocationContext Context, const VBitMap& Other) const
	{
		if (!Storage || !Other.Storage)
		{
			return Storage == Other.Storage;
		}
		return VInt::Eq(Context, Storage.Get(), Other.Storage.Get());
	}

	void SetBit(FAllocationContext Context, uint32 BitIndex)
	{
		checkSlow(BitIndex < Size());
		if (IsInt32())
		{
			Storage.Set(Context, VInt(AsInt32() | (1 << BitIndex)));
		}
		else
		{
			DigitRef(BitIndex) |= (1 << BitIndex % NumDigitBits);
		}
	}

	void UnsetBit(FAllocationContext Context, uint32 BitIndex)
	{
		checkSlow(BitIndex < Size());
		if (IsInt32())
		{
			Storage.Set(Context, VInt(AsInt32() & ~(1 << BitIndex)));
		}
		else
		{
			DigitRef(BitIndex) &= ~(1 << BitIndex % NumDigitBits);
		}
	}

	// returns true if bit is set
	bool CheckBit(uint32 BitIndex)
	{
		checkSlow(BitIndex < Size());
		if (IsInt32())
		{
			return AsInt32() & (1 << BitIndex);
		}
		else
		{
			return DigitRef(BitIndex) & (1 << BitIndex % NumDigitBits);
		}
	}

	void Clear(FAllocationContext Context)
	{
		if (IsInt32())
		{
			Storage.Set(Context, VInt(0));
		}
		else
		{
			VHeapInt& HeapInt = Storage.Get().StaticCast<VHeapInt>();
			memset(HeapInt.DataStorage(), 0, HeapInt.GetLength() * sizeof(VHeapInt::Digit));
		}
	}

	bool IsZero()
	{
		return Storage.Get().IsZero();
	}

	uint64 Size()
	{
		if (IsInt32())
		{
			return NumDigitBits;
		}
		else
		{
			VHeapInt& HeapInt = Storage.Get().StaticCast<VHeapInt>();
			return HeapInt.GetLength() * NumDigitBits;
		}
	}

	template <typename TVisitor>
	friend void Visit(TVisitor& Visitor, VBitMap& Value)
	{
		Visitor.Visit(Value.Storage, TEXT("Storage"));
	}

private:
	uint32& DigitRef(uint64 BitIndex)
	{
		return Storage.Get().StaticCast<VHeapInt>().DataStorage()[BitIndex / NumDigitBits];
	}

	int32 AsInt32()
	{
		return Storage.Get().AsInt32();
	}

	bool IsInt32()
	{
		return Storage.Get().IsInt32();
	}
};

inline uint32 GetTypeHash(const VBitMap& BitMap)
{
	return GetTypeHash(BitMap.Storage);
}

}; // namespace Verse

#endif // WITH_VERSE_VM
