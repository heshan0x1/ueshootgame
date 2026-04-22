// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Casts.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#if WITH_VERSE_BPVM
#include "UObject/ObjectPtr.h"
#endif
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMGlobalProgram.h"
#include "VerseVM/VVMVerseClass.h"
#include "VerseVM/VVMWriteBarrier.h"
#endif

namespace Verse
{
class FNativeType;
struct FNativeConverter;
template <typename NativeType, typename>
struct TToVValue;
struct VType;
} // namespace Verse

template <>
struct TIsZeroConstructType<Verse::FNativeType>
{
	static constexpr bool Value = true;
};

namespace Verse
{

enum class EDefaultConstructNativeType
{
	UnsafeDoNotUse
}; // So we can construct FNativeTypes

#if WITH_VERSE_BPVM
// Opaque wrapper around VM-specific type representation
class FNativeType
{
public:
	FNativeType() = delete;
	FNativeType(EDefaultConstructNativeType) {}

	FNativeType(UStruct* InType)
		: Type(InType)
	{
	}

	bool IsNullUnsafeDoNotUse() const { return !Type; }
	bool IsEqualUnsafeDoNotUse(const FNativeType& Other) const { return Type == Other.Type; }
	UStruct* AsUEStructNullableUnsafeDoNotUse() const { return Type.Get(); }
	UClass* AsUEClassNullableUnsafeDoNotUse() const { return Cast<UClass>(AsUEStructNullableUnsafeDoNotUse()); }
	UClass* AsUEClassCheckedUnsafeDoNotUse() const { return AsUEClassChecked(); }

protected:
	UClass* AsUEClassChecked() const
	{
		return CastChecked<UClass>(Type);
	}

	bool IsTypeOf(UObject* Obj) const
	{
		if (UClass* Class = Cast<UClass>(Type))
		{
			return Obj->IsA(Class);
		}
		return false;
	}

private:
	// This class is used via FObjectProperty, so must be a thin wrapper around this pointer.
	TObjectPtr<UStruct> Type;
};
#endif
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
// Opaque wrapper around VM-specific type representation
class FNativeType
{
public:
	FNativeType() = delete;
	FNativeType(EDefaultConstructNativeType) {}

	FNativeType(UStruct* InType)
	{
		FAllocationContext Context = FAllocationContextPromise{};
		VType* VerseType = nullptr;
		if (!InType)
		{
			VerseType = &Verse::GlobalFalse();
		}
		else if (UVerseClass* VerseClass = Cast<UVerseClass>(InType))
		{
			VerseType = VerseClass->Class.Get();
		}
		else if (UVerseStruct* VerseStruct = Cast<UVerseStruct>(InType))
		{
			VerseType = VerseStruct->Class.Get();
		}
		else
		{
			VerseType = Verse::GlobalProgram->LookupImport(Context, InType);
			V_DIE_UNLESS(VerseType);
		}

		Type.Set(Context, VerseType);
	}

	FNativeType(VType* InType)
	{
		FAllocationContext Context = FAllocationContextPromise{};
		Type.Set(Context, InType);
	}

	bool IsNullUnsafeDoNotUse() const { return !Type; }
	bool IsEqualUnsafeDoNotUse(const FNativeType& Other) const { return Type == Other.Type; }

	UStruct* AsUEStructNullableUnsafeDoNotUse() const
	{
		if (Type)
		{
			if (VClass* Class = Type->DynamicCast<VClass>())
			{
				return Class->GetUETypeChecked<UStruct>();
			}
		}
		return nullptr;
	}

	UClass* AsUEClassNullableUnsafeDoNotUse() const
	{
		return Cast<UClass>(AsUEStructNullableUnsafeDoNotUse());
	}

	UClass* AsUEClassCheckedUnsafeDoNotUse() const { return AsUEClassChecked(); }

	bool Subsumes(FAllocationContext Context, VCell& Object) const
	{
		return Type->Subsumes(Context, Object);
	}

	TSharedPtr<FJsonValue> ToJSON(FRunningContext Context, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth, FJsonObject* Defs) const
	{
		return Type->ToJSON(Context, Format, VisitedObjects, Callback, RecursionDepth, Defs);
	}
	VValue FromJSON(FRunningContext Context, const FJsonValue& JsonValue, EValueJSONFormat Format) const
	{
		return Type->FromJSON(Context, JsonValue, Format);
	}
	bool IsSubtype(const Verse::VType& InType)
	{
		return InType.GetCppClassInfo()->IsA(Type->GetCppClassInfo());
	}

protected:
	UClass* AsUEClassChecked() const
	{
		UClass* Result = AsUEClassNullableUnsafeDoNotUse();
		check(Result);
		return Result;
	}

	bool IsTypeOf(UObject* Obj) const
	{
		return Obj->IsA(AsUEClassNullableUnsafeDoNotUse());
	}

private:
	friend class ::FVRestValueProperty;
	friend struct Verse::FNativeConverter;
	template <typename NativeType, typename>
	friend struct Verse::TToVValue;

	// This class is used via FVRestValueProperty, so must be a thin wrapper around this pointer.
	TWriteBarrier<VType> Type;
};
#endif

template <class T, class BaseType = FNativeType>
class TNativeSubtype : public BaseType
{
public:
	TNativeSubtype() = delete;
	TNativeSubtype(EDefaultConstructNativeType)
		: BaseType(EDefaultConstructNativeType::UnsafeDoNotUse) {}
	TNativeSubtype(BaseType&& Other)
		: BaseType(MoveTemp(Other)) { CheckValid(); }
	TNativeSubtype(const BaseType& Other)
		: BaseType(Other) { CheckValid(); }
	TNativeSubtype& operator=(BaseType&& Other)
	{
		BaseType::operator=(MoveTemp(Other));
		CheckValid();
		return *this;
	}
	TNativeSubtype& operator=(const BaseType& Other)
	{
		BaseType::operator=(Other);
		CheckValid();
		return *this;
	}

	using BaseType::AsUEClassChecked;
	using BaseType::IsTypeOf;

	TNativeSubtype(UClass* InType)
		: BaseType(InType)
	{
		CheckValid();
	}

private:
	void CheckValid() const { check(AsUEClassChecked()->IsChildOf(T::StaticClass())); }
};

// Support for castable types

class FNativeCastableType : public FNativeType
{
public:
	using FNativeType::FNativeType;
	using FNativeType::IsTypeOf;
};

template <class T>
using TNativeCastableSubtype = TNativeSubtype<T, FNativeCastableType>;

// Support for concrete types

class FNativeConcreteType : public FNativeType
{
public:
	using FNativeType::FNativeType;
	using FNativeType::IsTypeOf;
};

template <class T>
using TNativeConcreteSubtype = TNativeSubtype<T, FNativeConcreteType>;

} // namespace Verse
