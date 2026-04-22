// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Math/MathFwd.h"
#include "Misc/TransformUtilities.h"
#include "Serialization/ArchiveCrc32.h"
#include "UObject/Class.h"

class UMaterialInterface;
class UTexture;
class UStaticMesh;
class USkinnedAsset;
class UObject;

class FHLODHashBuilder : public FArchiveCrc32
{
public:
	/** Push a new context */
	void PushObjectContext(const class UObject* InObjectContext);

	/** Pop context */
	void PopObjectContext();

	template <typename TParamType>
	typename TEnableIf<TIsIntegral<TParamType>::Value, FArchive&>::Type operator<<(TParamType InValue)
	{
		FArchive& Self = *this;
		return Self << InValue;
	}

	// Numeric / enum values will be logged as string (order preserved)
	template <typename T>
	typename TEnableIf< TIsIntegral<T>::Value || TIsFloatingPoint<T>::Value || TIsEnum<T>::Value, void>::Type
	HashField(const T& InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, FieldValueToString(InFieldValue));

		*this << const_cast<T&>(InFieldValue);
	}

	// Everything else will log a CRC32 as 8-hex string
	template <typename T>
	typename TEnableIf< !(TIsIntegral<T>::Value || TIsFloatingPoint<T>::Value || TIsEnum<T>::Value), void>::Type
	HashField(const T& InFieldValue, const FName& InFieldName)
	{
		FArchiveCrc32 FieldHashAr;
		FieldHashAr << const_cast<T&>(InFieldValue);

		AddField(InFieldName, FString::Printf(TEXT("%08X"), FieldHashAr.GetCrc()));

		*this << const_cast<T&>(InFieldValue);
	}

	void HashField(const FString& InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, InFieldValue);

		*this << const_cast<FString&>(InFieldValue);
	}

	void HashField(const FName& InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, InFieldValue.ToString());

		*this << const_cast<FName&>(InFieldValue);
	}

	void HashField(const ANSICHAR* InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, InFieldValue);

		FString StringValue(InFieldValue);
		*this << StringValue;
	}

	void HashField(const WIDECHAR* InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, InFieldValue);

		FString StringValue(InFieldValue);
		*this << StringValue;
	}

	void HashField(const FTransform& InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, FString::Printf(TEXT("%08X"), TransformUtilities::GetRoundedTransformCRC32(InFieldValue)));

		*this << const_cast<FTransform&>(InFieldValue);
	}

	template <typename T>
	void HashField(const TObjectPtr<T>& InFieldValue, const FName& InFieldName)
	{
		AddField(InFieldName, InFieldValue ? InFieldValue->GetPathName() : TEXT("<none>"));

		*this << const_cast<T*>(InFieldValue.Get());
	}

	ENGINE_API FArchive& operator<<(FTransform InTransform);
	ENGINE_API FArchive& operator<<(UMaterialInterface* InMaterialInterface);
	ENGINE_API FArchive& operator<<(UTexture* InTexture);
	ENGINE_API FArchive& operator<<(UStaticMesh* InStaticMesh);
	ENGINE_API FArchive& operator<<(USkinnedAsset* InSkinnedAsset);

	//~ Begin FArchive Interface
	ENGINE_API virtual FArchive& operator<<(UObject*& Object) override;
	//~ End FArchive Interface

	void LogContext(const TCHAR* Context, bool bOutputHash);

	// For visibility of the overloads we don't override
	using FArchiveCrc32::operator<<;

	ENGINE_API FString BuildHashReport() const;

private:
	uint32 AddAssetReference(UObject* Asset, TFunctionRef<uint32()> GetHashFunc);

	// Add a field to the current object context
	void AddField(const FName& InFieldName, const FString& InFieldValue)
	{
		FFieldArray& Fields = ObjectContextStack.IsEmpty() ? GlobalFields : ObjectsHashes.FindOrAdd(ObjectContextStack.Top()).Fields;
		Fields.Emplace(InFieldName, InFieldValue);
	}

	// Pretty enum string for UENUMs; falls back to the integer value if no UENUM is available.
	template <typename T>
	static FString EnumToPrettyString(T InEnumValue)
	{
		// non-null only for UENUMs
		if (const UEnum* EnumType = StaticEnum<T>())
		{
			const int64 RawValue = static_cast<int64>(InEnumValue);
			const FString Name = EnumType->GetNameStringByValue(RawValue);
			if (!Name.IsEmpty())
			{
				return Name + TEXT("(") + LexToString(static_cast<__underlying_type(T)>(InEnumValue)) + TEXT(")");
			}
		}

		// Fallback to the integer value
		return LexToString(static_cast<__underlying_type(T)>(InEnumValue));
	}

	// Helper to stringify fields
	template <typename T>
	static FString FieldValueToString(const T& InFieldValue)
	{
		if constexpr (std::is_same<T, bool>::value)
		{
			return InFieldValue ? TEXT("1") : TEXT("0");
		}
		else if constexpr (TIsEnum<T>::Value)
		{
			return EnumToPrettyString(InFieldValue);
		}
		else if constexpr (TIsFloatingPoint<T>::Value)
		{
			static const int32 Precision = 4;
			FString S = FString::Printf(TEXT("%.*f"), Precision, InFieldValue);
			
			while (S.Len() && S.EndsWith(TEXT("0")))
			{
				S.LeftChopInline(1);
			}
			
			if (S.Len() && S.EndsWith(TEXT(".")))
			{
				S.LeftChopInline(1);
			}

			return S.IsEmpty() ? TEXT("0") : S;
		}
		else if constexpr (TIsIntegral<T>::Value)
		{
			return LexToString(InFieldValue);
		}
		else
		{
			static_assert(!sizeof(T), "FieldValueToString only for numeric/enum types");
			return FString();
		}
	}

private:
	// For a given asset, store its hash & type.
	struct FAssetHash
	{
		uint32	Hash = 0;
		FName	AssetType = NAME_None;
	};
	TMap<FName, FAssetHash> AssetsHashes;

	// Array of name/value for fields.
	typedef TArray<TTuple<FName, FString>>	FFieldArray;

	// Track assets references & fields values for a given object.
	struct FObjectHash
	{
		uint32		Hash = 0;
		TSet<FName>	ReferencedAssets;
		FFieldArray Fields;
	};

	// Map of objects names to their data.
	TMap<FString, FObjectHash> ObjectsHashes;

	// Stack of objects.
	TArray<FString> ObjectContextStack;

	// Global hashing fields not tied to any particular object.
	FFieldArray GlobalFields;
};

// Templated operator overloads for handling arrays
template <typename TElementType, typename TAllocatorType>
FArchive& operator<<(FHLODHashBuilder& Ar, const TArray<TElementType, TAllocatorType>& InArray)
{
	TArray<TElementType, TAllocatorType>& ArrayMutable = const_cast<TArray<TElementType, TAllocatorType>&>(InArray);
	FArchive& Self = Ar;
	return Self << ArrayMutable;
}

template <typename TElementType, typename TAllocatorType>
FArchive& operator<<(FHLODHashBuilder& Ar, TArray<TElementType, TAllocatorType>& InArray)
{
	FArchive& Self = Ar;
	return Self << InArray;
}

class FHLODHashScope
{
public:
	enum class EFlags : uint8
	{
		None = 0,
		ResetHash = 1 << 0,		//!< Reset the hash when entering this scope
	};

	FHLODHashScope(FHLODHashBuilder& InBuilder, const UObject* InObjectContext, EFlags InFlags = EFlags::None)
		: Builder(InBuilder)
	{
		Builder.PushObjectContext(InObjectContext);

		if (EnumHasAnyFlags(InFlags, EFlags::ResetHash))
		{
			Builder.Reset();
		}
	}

	~FHLODHashScope()
	{
		Builder.PopObjectContext();
	}

private:
	FHLODHashBuilder& Builder;
};

#endif
