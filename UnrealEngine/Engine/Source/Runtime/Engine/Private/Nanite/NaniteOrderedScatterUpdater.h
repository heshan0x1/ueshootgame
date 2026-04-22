// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/HashTable.h"

class FRDGBuilder;
class FRDGBufferUAV;

namespace Nanite
{

enum class EScatterOp
{
	Or = 0,
	And = 1,
	Write = 2,
};

// Helper to emulate serial buffer updates on the GPU
class FOrderedScatterUpdater
{
public:
	FOrderedScatterUpdater(uint32 NumExpectedElements);

	void Add(EScatterOp Op, uint32 Offset, uint32 Value)
	{
		check(IsAligned(Offset, 4));

		Updates.Add(FUpdate(Op, Offset, Value));
	}

	// Call this if there can be multiple updates to the same address.
	// When executed on the GPU, these updates can otherwise be unordered.
	// For a given address ResolveOverwrites removes all updates except the last one.
	void ResolveOverwrites(bool bVerify);

	void Flush(FRDGBuilder& GraphBuilder, FRDGBufferUAV* UAV);
	
	uint32 Num() const
	{
		return Updates.Num();
	}

private:
	class FUpdate
	{
	public:
		FUpdate() :
			Op_Offset(0xFFFFFFFCu), Value(0u)
		{
		}

		FUpdate(EScatterOp InOp, uint32 InOffset, uint32 InValue) :
			Op_Offset(InOffset | (uint32)InOp),
			Value(InValue)
		{
		}

		EScatterOp	GetOp()		const { return EScatterOp(Op_Offset & 3u); }
		uint32		GetOffset() const { return Op_Offset & ~3u; }
		uint32		GetValue()	const { return Value; }

		uint32 WriteMask() const
		{
			switch (GetOp())
			{
			case EScatterOp::Or: return Value;
			case EScatterOp::And: return ~Value;
			case EScatterOp::Write: return 0xFFFFFFFFu;
			default:
				check(false);
				return 0xFFFFFFFFu;
			}
		}
	private:
		uint32 Op_Offset;
		uint32 Value;
	};

	FHashTable HashTable;
	TArray<FUpdate> Updates;
	uint32 HashShift;
};

} // namespace Nanite