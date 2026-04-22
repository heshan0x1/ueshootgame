// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Utf8String.h"
#include "Templates/SharedPointer.h"

namespace Verse
{
class FNativeString;
}

template <>
struct TIsZeroConstructType<Verse::FNativeString>
{
	static constexpr bool Value = true;
};

template <>
struct TIsContiguousContainer<Verse::FNativeString>
{
	static constexpr bool Value = true;
};

namespace Verse
{
// Wraps a copy-on-write reference to a Utf8String to give it Verse semantics,
// and since we copy objects around and don't modify them often in the VM gives
// us a performance uplift in heavy string workloads.
class FNativeString
{
	struct FCopyOnWriteContents final
	{
		FCopyOnWriteContents() = default;
		FCopyOnWriteContents(const FUtf8String& String)
			: String(String) {}
		FCopyOnWriteContents(FUtf8String&& String)
			: String(String) {}

		FUtf8String String;
		mutable int32 CachedHash = 0;

		inline int32 Hash() const
		{
			if (0 == CachedHash)
			{
				// Do not forward to GetTypeHash(const FUtf8String&), which is case-insensitive.
				CachedHash = FCrc::StrCrc32Len(*String, String.Len());

				// 0 is technically a valid hash result, but since we are using it to mean the
				// hash was not cached, if we get a hash result of 0 we modify the saved value.
				if (UNLIKELY(0 == CachedHash))
				{
					CachedHash = -1;
				}
			}

			return CachedHash;
		}

		UE_FORCEINLINE_HINT void ResetCachedHash()
		{
			CachedHash = 0;
		}
	};

	struct FCopyOnWrite final
	{
		UE_FORCEINLINE_HINT FCopyOnWrite() = default;
		UE_FORCEINLINE_HINT FCopyOnWrite(const FUtf8String& String)
			: Payload(MakeShared<FCopyOnWriteContents, ESPMode::ThreadSafe>(String)) {}
		UE_FORCEINLINE_HINT FCopyOnWrite(FUtf8String&& String)
			: Payload(MakeShared<FCopyOnWriteContents, ESPMode::ThreadSafe>(String)) {}

		const FUtf8String& Read() const
		{
			const_cast<FCopyOnWrite*>(this)->SetIfNull();
			return Payload->String;
		}

		FUtf8String& Write()
		{
			if (!SetIfNull())
			{
				if (!Payload.IsUnique())
				{
					Payload = MakeShared<FCopyOnWriteContents, ESPMode::ThreadSafe>(Payload->String);
				}

				// Since we are writing to the string we need to forget any previous cached hash we stored.
				Payload->ResetCachedHash();
			}

			return Payload->String;
		}

		int32 Hash() const
		{
			const_cast<FCopyOnWrite*>(this)->SetIfNull();
			return Payload->Hash();
		}

	private:
		TSharedPtr<FCopyOnWriteContents, ESPMode::ThreadSafe> Payload;

		static_assert(TIsZeroConstructType<decltype(Payload)>::Value);

		inline bool SetIfNull()
		{
			if (UNLIKELY(!Payload.IsValid()))
			{
				Payload = MakeShared<FCopyOnWriteContents, ESPMode::ThreadSafe>();
				return true;
			}

			return false;
		}
	};

public:
	using ElementType = FUtf8String::ElementType;

private:
	FCopyOnWrite Payload;

public:
	FNativeString() = default;
	FNativeString(FNativeString&&) = default;
	FNativeString(const FNativeString&) = default;
	FNativeString& operator=(FNativeString&&) = default;
	FNativeString& operator=(const FNativeString&) = default;

	UE_FORCEINLINE_HINT FNativeString(const ANSICHAR* Str)
		: Payload(Str) {}

	UE_FORCEINLINE_HINT FNativeString(FUtf8String&& InString)
		: Payload(MoveTemp(InString)) {}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& TIsCharType_V<CharRangeElementType>)>
	UE_FORCEINLINE_HINT explicit FNativeString(CharRangeType&& Range)
		: Payload(FUtf8String(Forward<CharRangeType>(Range)))
	{
	}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& std::is_same_v<ElementType, CharRangeElementType>)>
	inline FNativeString& operator=(CharRangeType&& Range)
	{
		Payload = FUtf8String(Forward<CharRangeType>(Range));
		return *this;
	}

	friend UE_FORCEINLINE_HINT ElementType* GetData(FNativeString& InString) { return GetData(InString.Payload.Write()); }
	friend UE_FORCEINLINE_HINT const ElementType* GetData(const FNativeString& InString) { return GetData(InString.Payload.Read()); }

	friend UE_FORCEINLINE_HINT int32 GetNum(const FNativeString& InString) { return GetNum(InString.Payload.Read()); }

	UE_FORCEINLINE_HINT ElementType& operator[](int32 Index) UE_LIFETIMEBOUND { return Payload.Write()[Index]; }
	UE_FORCEINLINE_HINT const ElementType& operator[](int32 Index) const UE_LIFETIMEBOUND { return Payload.Read()[Index]; }

	[[nodiscard]] UE_FORCEINLINE_HINT const ElementType* operator*() const UE_LIFETIMEBOUND { return *Payload.Read(); }

	[[nodiscard]] UE_FORCEINLINE_HINT int Len() const { return Payload.Read().Len(); }
	[[nodiscard]] UE_FORCEINLINE_HINT bool IsEmpty() const { return Payload.Read().IsEmpty(); }

	[[nodiscard]] UE_FORCEINLINE_HINT friend bool operator==(const FNativeString& Lhs, const FNativeString& Rhs)
	{
		// Do not forward to FUtf8String::operator==, which is case-insensitive.
		return Lhs.Equals(Rhs);
	}

	[[nodiscard]] UE_FORCEINLINE_HINT friend bool operator!=(const FNativeString& Lhs, const FNativeString& Rhs)
	{
		// Do not forward to FUtf8String::operator!=, which is case-insensitive.
		return !Lhs.Equals(Rhs);
	}

	[[nodiscard]] UE_FORCEINLINE_HINT bool Equals(const FNativeString& Other) const
	{
		return Payload.Read().Equals(Other.Payload.Read(), ESearchCase::CaseSensitive);
	}

	friend UE_FORCEINLINE_HINT int32 GetTypeHash(const FNativeString& S)
	{
		return S.Payload.Hash();
	}

	void Reset(int32 NewReservedSize = 0) { Payload.Write().Reset(NewReservedSize); }

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& TIsCharType_V<CharRangeElementType>)>
	inline FNativeString& operator+=(CharRangeType&& Str)
	{
		Payload.Write() += Forward<CharRangeType>(Str);
		return *this;
	}
	inline FNativeString& operator+=(const ANSICHAR* Str)
	{
		Payload.Write() += Str;
		return *this;
	}

	template <
		typename CharRangeType,
		typename CharRangeElementType = TElementType_T<CharRangeType>
			UE_REQUIRES(TIsContiguousContainer<CharRangeType>::Value&& std::is_same_v<ElementType, CharRangeElementType>)>
	inline friend FNativeString operator+(FNativeString&& Lhs, CharRangeType&& Rhs)
	{
		Lhs.Payload.Write() += Forward<CharRangeType>(Rhs);
		return Lhs;
	}
	inline friend FNativeString operator+(FNativeString&& Lhs, const ANSICHAR* Rhs)
	{
		Lhs.Payload.Write() += Rhs;
		return Lhs;
	}

	template <typename... Types>
	[[nodiscard]] UE_FORCEINLINE_HINT static FNativeString Printf(UE::Core::TCheckedFormatString<FUtf8String::FmtCharType, Types...> Fmt, Types... Args)
	{
		return FUtf8String::Printf(Fmt, Args...);
	}

	friend UE_FORCEINLINE_HINT FArchive& operator<<(FArchive& Ar, FNativeString& S) { return Ar << S.Payload.Write(); }

	static void AutoRTFMAssignFromOpenToClosed(FNativeString& Closed, const FNativeString& Open)
	{
		const AutoRTFM::EContextStatus Status = AutoRTFM::Close([&] { Closed = Open; });
		ensure(AutoRTFM::EContextStatus::OnTrack == Status);
	}
};
} // namespace Verse

// A more UHT friendly name for a verse native string
using FVerseString = Verse::FNativeString;
