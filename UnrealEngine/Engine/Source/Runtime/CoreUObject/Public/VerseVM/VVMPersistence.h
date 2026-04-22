// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "Delegates/Delegate.h"
#include "Templates/Function.h"
#include "VerseVM/VVMValue.h"

class FJsonValue;
class FJsonObject;

namespace Verse
{
COREUOBJECT_API TOptional<TMap<FString, TSharedPtr<FJsonValue>>> MapFromPersistentJson(const FJsonObject& JsonObject);

template <typename TShortName>
FString ShortNameToFieldName(TShortName&& ShortName)
{
	return "x_" + FString{::Forward<TShortName>(ShortName)};
}

template <typename TArg>
TOptional<TArg> NameToShortName(TArg PropertyName)
{
	int32 Index;
	// Remove everything up to and including the 'x' of "__verse_0x".
	if (!PropertyName.FindChar('x', Index))
	{
		return {};
	}
	PropertyName.RightChopInline(Index + 1);
	// Remove the hash followed by '_' that follows "__verse_0x".
	if (!PropertyName.FindChar('_', Index))
	{
		return {};
	}
	return PropertyName.RightChop(Index + 1);
}

struct VPersistentMap;

struct FUpdatedPersistentPairSave
{
	// Verse path of the update persistent `var` `weak_map`
	FString Path;
	// Key of the updated value in the map.
	void* Key;
	// Updated value in the map.
	TSharedPtr<FJsonValue> Value;

#if WITH_VERSE_BPVM
	// `FProperty` of the related persistent `var` `weak_map`
	const FMapProperty* MapProperty;
	// Updated value in the map. The structure of the value can be found in
	// `MapProperty->ValueProp`.
	void* RawValue;
#else
	VValue RawValue;
#endif
};

// Represents an update to a persistent `var` `weak_map`. A range of
// `FUpdatedPair`s is broadcast by `Updated`.
struct FUpdatedPersistentPairVM
{
	// Verse path of the update persistent `var` `weak_map`
	FString Path;
#if WITH_VERSE_BPVM
	// `FProperty` of the related persistent `var` `weak_map`
	const FMapProperty* MapProperty;
	// Key of the updated value in the map.
	void* Key;
	// Updated value in the map. The structure of the value can be found in
	// `MapProperty->ValueProp`.
	void* Value;
#else
	// Key of the updated value in the map.
	VValue Key;
	// Updated value in the map.
	VValue Value;
#endif

#if WITH_VERSE_BPVM
	FUpdatedPersistentPairVM(FString Path, const FMapProperty* MapProperty, void* Key, void* Value)
		: Path(::MoveTemp(Path))
		, MapProperty(MapProperty)
		, Key(Key)
		, Value(Value)
	{
	}
#else
	FUpdatedPersistentPairVM(FString Path, VValue Key, VValue Value)
		: Path(::MoveTemp(Path))
		, Key(Key)
		, Value(Value)
	{
	}
#endif

	friend bool operator==(const FUpdatedPersistentPairVM& Left, const FUpdatedPersistentPairVM& Right)
	{
#if WITH_VERSE_BPVM
		return Left.Key == Right.Key && FCString::Strcmp(*Left.Path, *Right.Path) == 0;
#else
		return Left.Key.GetEncodedBits() == Right.Key.GetEncodedBits() && FCString::Strcmp(*Left.Path, *Right.Path) == 0;
#endif
	}

	friend uint32 GetTypeHash(const FUpdatedPersistentPairVM& Arg)
	{
		return FCrc::StrCrc32(*Arg.Path);
	}

	friend bool operator<(const FUpdatedPersistentPairVM& Left, const FUpdatedPersistentPairVM& Right)
	{
#if WITH_VERSE_BPVM
		if (Left.Key < Right.Key)
#else
		if (Left.Key.GetEncodedBits() < Right.Key.GetEncodedBits())
#endif
		{
			return true;
		}
#if WITH_VERSE_BPVM
		if (Right.Key < Left.Key)
#else
		if (Right.Key.GetEncodedBits() < Left.Key.GetEncodedBits())
#endif
		{
			return false;
		}
		return FCString::Strcmp(*Left.Path, *Right.Path) < 0;
	}
};

class IVersePersistence
{
protected:
	virtual ~IVersePersistence() = default;

public:
	using TPersistablePredicateSave = TFunction<bool(const TSharedPtr<FJsonValue>&)>;
#if WITH_VERSE_BPVM
	using TPersistablePredicateVM = TFunction<bool(const FProperty*, void*)>;
#else
	using TPersistablePredicateVM = TFunction<bool(VValue)>;
#endif

	/**
	 * Get the predicate used for `FitsInPlayerMap` intrinsic
	 */
	virtual const TPersistablePredicateVM GetPersistablePredicate() const = 0;

	/**
	 * Set the predicate used for `FitsInPlayerMap` intrinsic
	 */
	virtual void SetPersistablePredicate(TPersistablePredicateSave) = 0;

#if WITH_VERSE_BPVM
	/**
	 * Notify of the construction of a persistent `var` `weak_map`
	 */
	virtual void AddPersistentMap(const FString& Path, const FMapProperty*, UObject* Container) = 0;
#else
	/**
	 * Notify of the construction of a persistent `var` `weak_map`
	 */
	virtual void AddPersistentMap(const FString& Path, VPersistentMap& Map) = 0;
#endif

	/**
	 * Update a set of key-value pairs in a persistent `var` `weak_map`
	 */
	virtual void UpdatePersistentPairs(const FUpdatedPersistentPairVM*, const FUpdatedPersistentPairVM*) = 0;

	/**
	 * Event indicating when a persistent `var` is constructed (correlating directly with a call to `AddPersistentMap`)
	 */
	DECLARE_EVENT_OneParam(IVersePersistence, FOnPersistentMapConstructed, const FString& Path);
	virtual FOnPersistentMapConstructed& OnPersistentMapConstructed() = 0;

	DECLARE_EVENT_OneParam(IVersePersistence, FOnPersistentPairsUpdated, const TArray<FUpdatedPersistentPairSave>&);
	virtual FOnPersistentPairsUpdated& OnPersistentPairsUpdated() = 0;

	DECLARE_EVENT(IVersePersistence, FOnPersistentMapsReleased);
	virtual FOnPersistentMapsReleased& OnPersistentMapsReleased() = 0;

	/**
	 * Persistent map key added event, triggered when a persistent map key is added to Verse
	 *
	 * @param Key the new key that was added
	 */
	DECLARE_EVENT_OneParam(IVersePersistence, FOnPersistentMapKeyAdded, void* /*Key*/);
	virtual FOnPersistentMapKeyAdded& OnPersistentMapKeyAdded() = 0;

	/**
	 * Persistent map key removed event, triggered when a persistent map key is removed from Verse
	 *
	 * @param Key the key that was removed
	 */
	DECLARE_EVENT_OneParam(IVersePersistence, FOnPersistentMapKeyRemoved, void* /*Key*/);
	virtual FOnPersistentMapKeyRemoved& OnPersistentMapKeyRemoved() = 0;

	/**
	 * Add key and value to a persistent `var` `weak_map` with Verse path `Path`
	 */
	virtual void AddPersistentPair(const FString& Path, const void* Key, const TSharedRef<const FJsonValue>& Value) = 0;

#if WITH_VERSE_BPVM
	/**
	 * Add key and value to a persistent `var` `weak_map` with Verse path `Path`
	 */
	virtual void AddPersistentPair(const FString& Path, const void* Key, const void* Data) = 0;
#else
	/**
	 * Add key and value to a persistent `var` `weak_map` with Verse path `Path`
	 */
	virtual void AddPersistentPair(const FString& Path, const void* Key, VValue Value) = 0;
#endif

	/**
	 * Remove key from all persistent `var` `weak_map`s
	 */
	virtual void RemovePersistentPairs(const void* Key) = 0;

	/**
	 * Reset persistence by reinitialization all `var`s to a
	 * default-constructed value
	 * @note this assumes the `var` started out with a
	 * default-constructed value
	 */
	virtual void ResetWeakMaps() = 0;
};
} // namespace Verse
