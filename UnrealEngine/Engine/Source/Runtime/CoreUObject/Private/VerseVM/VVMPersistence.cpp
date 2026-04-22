// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMPersistence.h"
#include "Dom/JsonObject.h"

namespace Verse
{

TOptional<TMap<FString, TSharedPtr<FJsonValue>>> MapFromPersistentJson(const FJsonObject& JsonObject)
{
	TMap<FString, TSharedPtr<FJsonValue>> JsonValues;
	for (auto&& [FieldKey, FieldJsonValue] : JsonObject.Values)
	{
		// Ignore dummy padding field, package name field, and class name field.
		int32 Index;
		if (FieldKey.FindChar('$', Index))
		{
			continue;
		}
		TOptional<FString> ShortName = NameToShortName(FieldKey);
		if (!ShortName)
		{
			return {};
		}
		JsonValues.Emplace(::MoveTemp(*ShortName), FieldJsonValue);
	}
	return JsonValues;
}

} // namespace Verse
