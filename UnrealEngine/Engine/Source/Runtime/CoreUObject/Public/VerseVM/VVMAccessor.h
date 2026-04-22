// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "HAL/Platform.h"
#include "VVMContext.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMEnumerator.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMUniqueString.h"
#include "VerseVM/VVMValue.h"

namespace Verse
{

struct VAccessor : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	static VAccessor& NewUninitialized(FAllocationContext Context, uint32 InNumAccessors)
	{
		FLayout Layout = CalcLayout(InNumAccessors);
		return *new (Context.AllocateFastCell(Layout.TotalSize)) VAccessor(Context, InNumAccessors);
	}

	static void SerializeLayout(FAllocationContext Context, VAccessor*& This, FStructuredArchiveVisitor& Visitor);
	COREUOBJECT_API void SerializeImpl(FAllocationContext Context, FStructuredArchiveVisitor& Visitor);

	// `NumParams` -1 and -2 for getters and setters respectively to align them to trailing array positions
	TWriteBarrier<VUniqueString>* FindGetter(uint32 NumParams)
	{
		return (GetGettersBegin() + (NumParams - 1));
	}
	TWriteBarrier<VUniqueString>* FindSetter(uint32 NumParams)
	{
		return (GetSettersBegin() + (NumParams - 2));
	}
	void AddGetter(FAllocationContext Context, uint32 NumParams, VUniqueString& Name)
	{
		FindGetter(NumParams)->Set(Context, Name);
	}
	void AddSetter(FAllocationContext Context, uint32 NumParams, VUniqueString& Name)
	{
		FindSetter(NumParams)->Set(Context, Name);
	}

private:
	friend struct VAccessChain;
	TWriteBarrier<VUniqueString>* GetGettersBegin() { return BitCast<TWriteBarrier<VUniqueString>*>(BitCast<std::byte*>(this) + GetLayout().GettersOffset); }
	TWriteBarrier<VUniqueString>* GetGettersEnd() { return GetGettersBegin() + NumAccessors; }

	TWriteBarrier<VUniqueString>* GetSettersBegin() { return BitCast<TWriteBarrier<VUniqueString>*>(BitCast<std::byte*>(this) + GetLayout().SettersOffset); }
	TWriteBarrier<VUniqueString>* GetSettersEnd() { return GetSettersBegin() + NumAccessors; }

	uint32 NumAccessors;
	TWriteBarrier<VCell> Trailing[];

	struct FLayout
	{
		int32 GettersOffset;
		int32 SettersOffset;
		int32 TotalSize;
	};

	static FLayout CalcLayout(uint32 NumAccessors)
	{
		FStructBuilder StructBuilder;
		StructBuilder.AddMember(offsetof(VAccessor, Trailing), alignof(VAccessor));

		FLayout Layout;
		Layout.GettersOffset = StructBuilder.AddMember(sizeof(TWriteBarrier<VUniqueString>) * NumAccessors, alignof(TWriteBarrier<VUniqueString>));
		Layout.SettersOffset = StructBuilder.AddMember(sizeof(TWriteBarrier<VUniqueString>) * NumAccessors, alignof(TWriteBarrier<VUniqueString>));
		Layout.TotalSize = StructBuilder.GetSize();
		return Layout;
	}

	FLayout GetLayout() const
	{
		return CalcLayout(NumAccessors);
	}

	VAccessor(FAllocationContext Context, uint32 InNumAccessors)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, NumAccessors(InNumAccessors)
	{
		for (TWriteBarrier<VUniqueString>* Getter = GetGettersBegin(); Getter != GetGettersEnd(); ++Getter)
		{
			new (Getter) TWriteBarrier<VUniqueString>{};
		}
		for (TWriteBarrier<VUniqueString>* Setter = GetSettersBegin(); Setter != GetGettersEnd(); ++Setter)
		{
			new (Setter) TWriteBarrier<VUniqueString>{};
		}
	}
};

struct VAccessChain : VHeapValue
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VHeapValue);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	COREUOBJECT_API static TGlobalHeapPtr<VEnumerator> AccessorEnum;

	uint32 ChainNum;
	TWriteBarrier<VAccessor> Accessor;
	TWriteBarrier<VValue> Self;

	static VAccessChain& New(FAllocationContext Context, VAccessor* InAccessor, VValue InSelf)
	{
		FLayout Layout = CalcLayout(InAccessor->NumAccessors);
		return *new (Context.AllocateFastCell(Layout.TotalSize)) VAccessChain(Context, InAccessor, InSelf);
	}

	static VAccessChain& New(FAllocationContext Context, VAccessChain& Other, VValue Value)
	{
		FLayout Layout = CalcLayout(Other.Accessor->NumAccessors);
		return *new (Context.AllocateFastCell(Layout.TotalSize)) VAccessChain(Context, Other, Value);
	}

	TArrayView<TWriteBarrier<VValue>> GetChain()
	{
		return TArrayView<TWriteBarrier<VValue>>(GetChainBegin(), ChainNum);
	}

private:
	TWriteBarrier<VCell> Trailing[];

	void Add(FAllocationContext Context, VValue Value)
	{
		checkSlow(ChainNum + 1 <= MaxArgs());
		(GetChainBegin() + ChainNum)->Set(Context, Value);
		++ChainNum;
	}

	TWriteBarrier<VValue>* GetChainBegin() { return BitCast<TWriteBarrier<VValue>*>(BitCast<std::byte*>(this) + GetLayout().ChainOffset); }
	TWriteBarrier<VValue>* GetChainEnd() { return GetChainBegin() + MaxArgs(); }

	struct FLayout
	{
		int32 ChainOffset;
		int32 TotalSize;
	};

	uint32 MaxArgs()
	{
		return Accessor->NumAccessors + 1;
	}

	static FLayout CalcLayout(uint32 NumAccessors)
	{
		FStructBuilder StructBuilder;
		StructBuilder.AddMember(offsetof(VAccessChain, Trailing), alignof(VAccessChain));

		FLayout Layout;
		Layout.ChainOffset = StructBuilder.AddMember(sizeof(TWriteBarrier<VValue>) * NumAccessors + 1, alignof(TWriteBarrier<VValue>));
		Layout.TotalSize = StructBuilder.GetSize();
		return Layout;
	}

	FLayout GetLayout() const
	{
		return CalcLayout(Accessor->NumAccessors);
	}

	VAccessChain(FAllocationContext Context, VAccessor* InAccessor, VValue InSelf)
		: VHeapValue(Context, &GlobalTrivialEmergentType.Get(Context))
		, ChainNum(0)
		, Accessor(Context, InAccessor)
		, Self(Context, InSelf)
	{
		checkSlow(Accessor.Get() != nullptr);
		for (TWriteBarrier<VValue>* Arg = GetChainBegin(); Arg != GetChainEnd(); ++Arg)
		{
			new (Arg) TWriteBarrier<VValue>{};
		}
		Add(Context, *AccessorEnum.Get());
	}

	VAccessChain(FAllocationContext Context, VAccessChain& Other, VValue Value)
		: VAccessChain(Context, Other.Accessor.Get(), Other.Self.Get())
	{
		ChainNum = Other.ChainNum;
		for (uint32 Index = 0; Index < ChainNum; ++Index)
		{
			(GetChainBegin() + Index)->Set(Context, (Other.GetChainBegin() + Index)->Get());
		}
		Add(Context, Value);
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
