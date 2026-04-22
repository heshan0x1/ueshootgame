// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dom/JsonObject.h"

namespace Verse
{
struct VCell;
struct VValue;

// constants for Persistence JSON
namespace Persistence
{
constexpr TCHAR PackageNameKey[] = TEXT("$package_name");
constexpr TCHAR ClassNameKey[] = TEXT("$class_name");
constexpr TCHAR KeyKey[] = TEXT("k");
constexpr TCHAR ValueKey[] = TEXT("v");
} // namespace Persistence

// constants for Persona JSON
namespace Persona
{
constexpr TCHAR StringString[] = TEXT("STRING");
constexpr TCHAR NumberString[] = TEXT("NUMBER");
constexpr TCHAR ObjectString[] = TEXT("OBJECT");
constexpr TCHAR ArrayString[] = TEXT("ARRAY");
constexpr TCHAR BooleanString[] = TEXT("BOOLEAN");
constexpr TCHAR IntegerString[] = TEXT("INTEGER");
constexpr TCHAR EnumString[] = TEXT("ENUM");

constexpr TCHAR ItemsString[] = TEXT("items");
constexpr TCHAR MaximumString[] = TEXT("maximum");
constexpr TCHAR MinimumString[] = TEXT("minimum");
constexpr TCHAR PropertiesString[] = TEXT("properties");
constexpr TCHAR RequiredString[] = TEXT("required");
constexpr TCHAR TypeString[] = TEXT("type");
constexpr TCHAR AnyOfString[] = TEXT("any_of");
constexpr TCHAR KeyString[] = TEXT("key");
constexpr TCHAR ValueString[] = TEXT("value");

constexpr TCHAR SchemaString[] = TEXT("$schema");
constexpr TCHAR SchemaLink[] = TEXT("https://ai.google.dev/api/caching#Schema");
} // namespace Persona

enum class EValueJSONFormat
{
	Analytics,   // Basic JSON, no schema
	Persistence, // Persistence JSON
	Persona      // Persona JSON, types implementing this should output themselves according to https://ai.google.dev/api/caching#Schema
};

enum class EVisitState
{
	Visiting,
	Visited
};

// To handle case differences for field names in Persona/Analytics which are otherwise the same, as analytics prefers PascalCase
#define PERSONA_FIELD(Name) Format == EValueJSONFormat::Persona ? Persona::Name##String : TEXT(#Name)

#if WITH_VERSE_VM
struct FRunningContext;

// This callback is used by `VValue::ToJSON` and is called before any other evaluation is done to allow callers to add custom handling
// If it returs nullptr, default handling is used
// Note: be careful of calling `VCell::ToJson` directly as that bypasses this callback
using VerseVMToJsonCallback = TFunction<TSharedPtr<FJsonValue>(FRunningContext Context, VValue Value, EValueJSONFormat Format, TMap<const void*, EVisitState>& VisitedObjects, uint32 RecursionDepth, FJsonObject* Defs)>;

COREUOBJECT_API TSharedPtr<FJsonValue> ToJSON(FRunningContext Context, VValue Value, EValueJSONFormat Format, const VerseVMToJsonCallback& Callback, uint32 RecursionDepth = 0, FJsonObject* Defs = nullptr);
#endif

COREUOBJECT_API TSharedRef<FJsonValue> Int64ToJson(int64 Arg);

COREUOBJECT_API bool TryGetInt64(const FJsonValue& JsonValue, int64& Int64Value);

COREUOBJECT_API TSharedPtr<FJsonValue> Wrap(const TSharedPtr<FJsonValue>& Value, EValueJSONFormat Format);
COREUOBJECT_API TSharedPtr<FJsonValue> Unwrap(const TSharedPtr<FJsonValue>& Value, EValueJSONFormat Format);

COREUOBJECT_API TSharedPtr<FJsonValue> Wrap(const TSharedPtr<FJsonValue>& Value);
COREUOBJECT_API TSharedPtr<FJsonValue> Unwrap(const FJsonValue& Value);

} // namespace Verse
