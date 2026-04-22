// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Internationalization/InternationalizationManifest.h"
#include "Internationalization/LocKeyFuncs.h"
#include "JsonUtils/RapidJsonUtils.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#define UE_API LOCALIZATION_API

class FInternationalizationManifest;
class FManifestEntry;

/**
 * Implements a serializer that serializes to and from Json encoded data.
 */
class FJsonInternationalizationManifestSerializer
{
public:

	/**
	 * Deserializes a Internationalization manifest from a JSON string.
	 *
	 * @param InStr The JSON string to deserialize from.
	 * @param Manifest The populated Internationalization manifest.
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeManifestFromString( const FString& InStr, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName = FName() );

	/**
	 * Deserializes a Internationalization manifest from a JSON file.
	 *
	 * @param InJsonFile The path to the JSON file to deserialize from.
	 * @param Manifest The populated Internationalization manifest.
	 * @return true if deserialization was successful, false otherwise.
	 */
	static UE_API bool DeserializeManifestFromFile( const FString& InJsonFile, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName = FName() );

	/**
	 * Serializes a Internationalization manifest to a JSON string.
	 *
	 * @param Manifest The Internationalization manifest data to serialize.
	 * @param Str The string to serialize into.
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeManifestToString( TSharedRef< const FInternationalizationManifest > Manifest, FString& Str );

	/**
	 * Serializes a Internationalization manifest to a JSON file.
	 *
	 * @param Manifest The Internationalization manifest data to serialize.
	 * @param InJsonFile The path to the JSON file to serialize to.
	 * @return true if serialization was successful, false otherwise.
	 */
	static UE_API bool SerializeManifestToFile( TSharedRef< const FInternationalizationManifest > Manifest, const FString& InJsonFile );

	/**
	 * Sort an Internationalization manifest as if it had been serialized and then deserialized from JSON.
	 */
	static UE_API void SortManifest(const TSharedRef<FInternationalizationManifest>& Manifest);

private:

	struct FStructuredEntry
	{
		explicit FStructuredEntry(const FString& InNamespace)
			: Namespace(InNamespace)
		{
		}

		const FString Namespace;
		TMap<FString, TSharedPtr<FStructuredEntry>, FDefaultSetAllocator, FLocKeyMapFuncs<TSharedPtr<FStructuredEntry>>> SubNamespaces;
		TArray<TSharedPtr<FManifestEntry>> ManifestEntries;
	};

	/**
	 * Convert a JSON object to a Internationalization manifest.
	 *
	 * @param InJsonObj The JSON object to serialize from.
	 * @param Manifest The Internationalization manifest that will store the data.
	 *
	 * @return true if deserialization was successful, false otherwise.
	 */
	static bool DeserializeInternal( UE::Json::FConstObject InJsonObj, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName );

	/**
	 * Convert a Internationalization manifest to a JSON object.
	 *
	 * @param InManifest The Internationalization manifest object to serialize from.
	 * @param JsonObj The Json object that will store the data.
	 *
	 * @return true if serialization was successful, false otherwise.
	 */
	static bool SerializeInternal( TSharedRef< const FInternationalizationManifest > InManifest, UE::Json::FObject JsonObj, UE::Json::FAllocator& Allocator );

	/**
	 * Recursive function that will traverse the JSON object and populate a Internationalization manifest.
	 *
	 * @param InJsonObj The JSON object.
	 * @param InNamespace The namespace of the parent JSON object.
	 * @param Manifest The Internationalization manifest that will store the data.
	 *
	 * @return true if successful, false otherwise.
	 */
	static bool JsonObjToManifest( UE::Json::FConstObject InJsonObj, FString InNamespace, TSharedRef< FInternationalizationManifest > Manifest, const FName PlatformName );

	/**
	 * Takes a Internationalization manifest and arranges the data into a hierarchy based on namespace.
	 *
	 * @param InManifest The Internationalization manifest.
	 * @param RootElement The root element of the structured data.
	 */
	static void GenerateStructuredData( TSharedRef< const FInternationalizationManifest > InManifest, TSharedPtr< FStructuredEntry > RootElement );

	/**
	 * Goes through the structured, hierarchy based, manifest data and does a non-culture specific sort on namespaces, default text, and key.
	 *
	 * @param RootElement The root element of the structured data.
	 */
	static void SortStructuredData( TSharedPtr< FStructuredEntry > InElement );

	/**
	 * Populates an Internationalization manifest from data that has been structured based on namespace.
	 *
	 * @param InElement The structured data.
	 * @param Manifest The Internationalization manifest that will store the data.
	 */
	static void StructuredDataToManifest(const TSharedPtr<const FStructuredEntry>& InElement, const TSharedRef<FInternationalizationManifest>& Manifest);

	/**
	 * Populates a JSON object from Internationalization manifest data that has been structured based on namespace.
	 *
	 * @param InElement Internationalization manifest data structured based on namespace.
	 * @param JsonObj JSON object to be populated.
	 */
	static void StructuredDataToJsonObj( TSharedPtr< const FStructuredEntry> InElement, UE::Json::FObject JsonObj, UE::Json::FAllocator& Allocator );


	static const FString TAG_FORMATVERSION;
	static const FString TAG_NAMESPACE;
	static const FString TAG_CHILDREN;
	static const FString TAG_SUBNAMESPACES;
	static const FString TAG_PATH;
	static const FString TAG_OPTIONAL;
	static const FString TAG_KEYCOLLECTION;
	static const FString TAG_KEY;
	static const FString TAG_DEPRECATED_DEFAULTTEXT;
	static const FString TAG_SOURCE;
	static const FString TAG_SOURCE_TEXT;
	static const FString TAG_PLATFORM_NAME;
	static const FString TAG_METADATA;
	static const FString TAG_METADATA_INFO;
	static const FString TAG_METADATA_KEY;
	static const FString NAMESPACE_DELIMITER;
};

#undef UE_API
