// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSparseSpanArray.h:
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SpanAllocator.h"

// Sparse array with stable indices and contiguous span allocation
template <typename ElementType>
class TSparseSpanArray
{
public:
	int32 Num() const
	{
		return SpanAllocator.GetMaxSize();
	}

	void Reserve(int32 NumElements)
	{
		Elements.Reserve(NumElements);
	}

	int32 AddSpan(int32 NumElements)
	{
		check(NumElements > 0);

		const int32 InsertIndex = SpanAllocator.Allocate(NumElements);
		
		// Resize element array
		if (SpanAllocator.GetMaxSize() > Elements.Num())
		{
			const int32 NumElementsToAdd = SpanAllocator.GetMaxSize() - Elements.Num();
			Elements.AddDefaulted(NumElementsToAdd);
			AllocatedElementsBitArray.Add(false, NumElementsToAdd);
		}

		// Reuse existing elements
		for (int32 ElementIndex = InsertIndex; ElementIndex < InsertIndex + NumElements; ++ElementIndex)
		{
			checkSlow(!IsAllocated(ElementIndex));
			Elements[ElementIndex] = ElementType();
		}

		AllocatedElementsBitArray.SetRange(InsertIndex, NumElements, true);

		return InsertIndex;
	}

	void RemoveSpan(int32 FirstElementIndex, int32 NumElements)
	{
		check(NumElements > 0);

		for (int32 ElementIndex = FirstElementIndex; ElementIndex < FirstElementIndex + NumElements; ++ElementIndex)
		{
			checkSlow(IsAllocated(ElementIndex));
			Elements[ElementIndex] = ElementType();
		}

		SpanAllocator.Free(FirstElementIndex, NumElements);
		AllocatedElementsBitArray.SetRange(FirstElementIndex, NumElements, false);
	}

	void Consolidate()
	{
		SpanAllocator.Consolidate();

		if (Elements.Num() > SpanAllocator.GetMaxSize() * ShrinkThreshold)
		{
			Elements.SetNum(SpanAllocator.GetMaxSize());
			AllocatedElementsBitArray.SetNumUninitialized(SpanAllocator.GetMaxSize());
		}
	}

	void Reset()
	{
		Elements.Reset();
		SpanAllocator.Reset();
		AllocatedElementsBitArray.SetNumUninitialized(0);
	}

	ElementType& operator[](int32 Index)
	{
		checkSlow(IsAllocated(Index));
		return Elements[Index];
	}

	const ElementType& operator[](int32 Index) const
	{
		checkSlow(IsAllocated(Index));
		return Elements[Index];
	}

	bool IsAllocated(int32 ElementIndex) const
	{
		if (ElementIndex < Num())
		{
			return AllocatedElementsBitArray[ElementIndex];
		}

		return false;
	}

	SIZE_T GetAllocatedSize() const
	{
		return Elements.GetAllocatedSize() + AllocatedElementsBitArray.GetAllocatedSize() + SpanAllocator.GetAllocatedSize();
	}

	class TRangedForIterator
	{
	public:
		TRangedForIterator(TSparseSpanArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.Num() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			} 
		}

		TRangedForIterator operator++()
		{
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.Num() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForIterator& Other) const
		{
			return ElementIndex != Other.ElementIndex;
		}

		ElementType& operator*()
		{
			return Array.Elements[ElementIndex];
		}

	private:
		TSparseSpanArray<ElementType>& Array;
		int32 ElementIndex;
	};

	class TRangedForConstIterator
	{
	public:
		TRangedForConstIterator(const TSparseSpanArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.Num() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			}
		}

		TRangedForConstIterator operator++()
		{ 
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.Num() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForConstIterator& Other) const
		{ 
			return ElementIndex != Other.ElementIndex;
		}

		const ElementType& operator*() const
		{ 
			return Array.Elements[ElementIndex];
		}

	private:
		const TSparseSpanArray<ElementType>& Array;
		int32 ElementIndex;
	};

	// Iterate over all allocated elements (skip free ones)
	TRangedForIterator begin() { return TRangedForIterator(*this, 0); }
	TRangedForIterator end() { return TRangedForIterator(*this, Num()); }
	TRangedForConstIterator begin() const { return TRangedForConstIterator(*this, 0); }
	TRangedForConstIterator end() const { return TRangedForConstIterator(*this, Num()); }

private:
	// Allocated size needs to be this much bigger than used size before we shrink this array
	static constexpr int32 ShrinkThreshold = 2;

	TArray<ElementType> Elements;
	TBitArray<> AllocatedElementsBitArray;
	FSpanAllocator SpanAllocator;
};

template <typename ElementType, uint32 BytesPerChunk = 2 * 1024 * 1024>
class TChunkedSparseArray
{
public:
	TChunkedSparseArray() = default;

	TChunkedSparseArray(const TChunkedSparseArray& Other)
	{
		*this = Other;
	}

	TChunkedSparseArray(TChunkedSparseArray&& Other)
	{
		*this = MoveTemp(Other);
	}

	TChunkedSparseArray& operator=(const TChunkedSparseArray& Other)
	{
		if (&Other != this)
		{
			Empty();

			FreeElementIndexHint = Other.FreeElementIndexHint;
			NumAllocatedElements = Other.NumAllocatedElements;
			MaxAllocatedElementIndexPlusOne = Other.MaxAllocatedElementIndexPlusOne;
			AllocatedElementsBitArray = Other.AllocatedElementsBitArray;

			const int32 NumElementChunks = Other.ElementChunks.Num();
			ElementChunks.Reserve(NumElementChunks);
			for (int32 ChunkIndex = 0; ChunkIndex < NumElementChunks; ++ChunkIndex)
			{
				ElementChunks.Add((ElementType*)FMemory::Malloc(BytesPerChunk));
			}

			for (TConstSetBitIterator It(AllocatedElementsBitArray); It && It.GetIndex() < GetMaxSize(); ++It)
			{
				const int32 ElementIndex = It.GetIndex();
				ElementType& Element = (*this)[ElementIndex];
				new (&Element) ElementType(Other[ElementIndex]);
			}
		}

		return *this;
	}

	TChunkedSparseArray& operator=(TChunkedSparseArray&& Other)
	{
		if (&Other != this)
		{
			Empty();
			
			FreeElementIndexHint = Other.FreeElementIndexHint;
			NumAllocatedElements = Other.NumAllocatedElements;
			MaxAllocatedElementIndexPlusOne = Other.MaxAllocatedElementIndexPlusOne;
			Other.FreeElementIndexHint = 0;
			Other.NumAllocatedElements = 0;
			Other.MaxAllocatedElementIndexPlusOne = 0;

			AllocatedElementsBitArray = MoveTemp(Other.AllocatedElementsBitArray);
			ElementChunks = MoveTemp(Other.ElementChunks);
		}

		return *this;
	}

	~TChunkedSparseArray()
	{
		Empty();
	}

	void Empty()
	{
		for (ElementType& Element : *this)
		{
			Element.~ElementType();
		}

		for (ElementType* Chunk : ElementChunks)
		{
			FMemory::Free(Chunk);
		}

		ElementChunks.Empty();
		AllocatedElementsBitArray.Empty();
		FreeElementIndexHint = 0;
		NumAllocatedElements = 0;
		MaxAllocatedElementIndexPlusOne = 0;
	}

	int32 GetMaxSize() const
	{
		return MaxAllocatedElementIndexPlusOne;
	}

	void Reserve(int32 NumElements)
	{
		const int32 NewNumChunks = FMath::DivideAndRoundUp(NumElements, ElementsPerChunk);
		
		AllocatedElementsBitArray.SetNum(FMath::Max(ElementChunks.Num(), NewNumChunks) * ElementsPerChunk, false);

		while (ElementChunks.Num() < NewNumChunks)
		{
			ElementChunks.Add((ElementType*)FMemory::Malloc(BytesPerChunk));
		}
	}

	int32 AddDefaulted()
	{
		// Find the first unused element slot or add to the end
		int32 InsertIndex;
		if (NumAllocatedElements == MaxAllocatedElementIndexPlusOne)
		{
			check(MaxAllocatedElementIndexPlusOne <= AllocatedElementsBitArray.Num());
			InsertIndex = MaxAllocatedElementIndexPlusOne;
			++MaxAllocatedElementIndexPlusOne;

			if (InsertIndex < AllocatedElementsBitArray.Num())
			{
				AllocatedElementsBitArray[InsertIndex] = true;
			}
		}
		else
		{
			check(NumAllocatedElements < MaxAllocatedElementIndexPlusOne);
			InsertIndex = AllocatedElementsBitArray.FindAndSetFirstZeroBit(FreeElementIndexHint);
			check(InsertIndex < MaxAllocatedElementIndexPlusOne);
		}

		FreeElementIndexHint = InsertIndex + 1;
		++NumAllocatedElements;

		// Grow by one chunk if we run out of space
		if (MaxAllocatedElementIndexPlusOne > ElementChunks.Num() * ElementsPerChunk)
		{
			check(InsertIndex + 1 == MaxAllocatedElementIndexPlusOne);
			check(InsertIndex == ElementChunks.Num() * ElementsPerChunk);
			check(AllocatedElementsBitArray.Num() == ElementChunks.Num() * ElementsPerChunk);

			ElementChunks.Add((ElementType*)FMemory::Malloc(BytesPerChunk));
			AllocatedElementsBitArray.Add(false, ElementsPerChunk);
			AllocatedElementsBitArray[InsertIndex] = true;
		}

		check(IsAllocated(InsertIndex));
		const int32 ChunkIndex = InsertIndex / ElementsPerChunk;
		const int32 Index = InsertIndex - ChunkIndex * ElementsPerChunk;
		// We can construct in-place because the element is either uninitialized or has already been destructed
		new (&ElementChunks[ChunkIndex][Index]) ElementType;

		return InsertIndex;
	}

	void RemoveAt(int32 ElementIndex)
	{
		check(IsAllocated(ElementIndex));
		const int32 ChunkIndex = ElementIndex / ElementsPerChunk;
		const int32 Index = ElementIndex - ChunkIndex * ElementsPerChunk;
		ElementChunks[ChunkIndex][Index].~ElementType();

		AllocatedElementsBitArray[ElementIndex] = false;

		--NumAllocatedElements;
		FreeElementIndexHint = FMath::Min(FreeElementIndexHint, ElementIndex);
		if (ElementIndex + 1 == MaxAllocatedElementIndexPlusOne)
		{
			MaxAllocatedElementIndexPlusOne = AllocatedElementsBitArray.FindLastFrom(true, ElementIndex - 1) + 1;
		}
		check(FreeElementIndexHint <= MaxAllocatedElementIndexPlusOne);
	}

	void Shrink()
	{
		// Try to keep an additional chunk if the last used chunk is more than half full
		const int32 ChunksToKeep = FMath::DivideAndRoundUp(MaxAllocatedElementIndexPlusOne + ElementsPerChunk / 2, ElementsPerChunk);
		
		AllocatedElementsBitArray.SetNumUninitialized(FMath::Min(ElementChunks.Num(), ChunksToKeep) * ElementsPerChunk);

		while (ElementChunks.Num() > ChunksToKeep)
		{
			FMemory::Free(ElementChunks.Last());
			ElementChunks.Pop(EAllowShrinking::No);
		}
	}

	ElementType& operator[](int32 ElementIndex)
	{
		check(IsAllocated(ElementIndex));
		const int32 ChunkIndex = ElementIndex / ElementsPerChunk;
		const int32 Index = ElementIndex - ChunkIndex * ElementsPerChunk;
		return ElementChunks[ChunkIndex][Index];
	}

	const ElementType& operator[](int32 ElementIndex) const
	{
		check(IsAllocated(ElementIndex));
		const int32 ChunkIndex = ElementIndex / ElementsPerChunk;
		const int32 Index = ElementIndex - ChunkIndex * ElementsPerChunk;
		return ElementChunks[ChunkIndex][Index];
	}

	bool IsAllocated(int32 ElementIndex) const
	{
		if (ElementIndex < GetMaxSize())
		{
			return AllocatedElementsBitArray[ElementIndex];
		}

		return false;
	}

	SIZE_T GetAllocatedSize() const
	{
		return ElementChunks.Num() * BytesPerChunk + ElementChunks.GetAllocatedSize() + AllocatedElementsBitArray.GetAllocatedSize() + sizeof(TChunkedSparseArray);
	}

	class TRangedForIterator
	{
	public:
		TRangedForIterator(TChunkedSparseArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.GetMaxSize() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			}
		}

		TRangedForIterator operator++()
		{
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.GetMaxSize() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForIterator& Other) const
		{
			return ElementIndex != Other.ElementIndex;
		}

		ElementType& operator*()
		{
			return Array[ElementIndex];
		}

	private:
		TChunkedSparseArray<ElementType>& Array;
		int32 ElementIndex;
	};

	class TRangedForConstIterator
	{
	public:
		TRangedForConstIterator(const TChunkedSparseArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.GetMaxSize() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			}
		}

		TRangedForConstIterator operator++()
		{
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.GetMaxSize() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForConstIterator& Other) const
		{
			return ElementIndex != Other.ElementIndex;
		}

		const ElementType& operator*() const
		{
			return Array[ElementIndex];
		}

	private:
		const TChunkedSparseArray<ElementType>& Array;
		int32 ElementIndex;
	};

	// Iterate over all allocated elements (skip free ones)
	TRangedForIterator begin() { return TRangedForIterator(*this, 0); }
	TRangedForIterator end() { return TRangedForIterator(*this, GetMaxSize()); }
	TRangedForConstIterator begin() const { return TRangedForConstIterator(*this, 0); }
	TRangedForConstIterator end() const { return TRangedForConstIterator(*this, GetMaxSize()); }

private:
	static constexpr int32 ElementsPerChunk = BytesPerChunk / sizeof(ElementType);

	int32 FreeElementIndexHint = 0;
	int32 NumAllocatedElements = 0;
	int32 MaxAllocatedElementIndexPlusOne = 0;

	TArray<ElementType*> ElementChunks;
	TBitArray<> AllocatedElementsBitArray;
};
