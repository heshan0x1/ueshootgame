// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/ExtensionData.h"

#define UE_API CUSTOMIZABLEOBJECTEDITOR_API

struct FInstancedStruct;
struct FMutableGraphGenerationContext;

/**
 * An object that gets passed around during Customizable Object compilation to help set up
 * Extension Data.
 */
class FExtensionDataCompilerInterface
{
public:
	UE_API FExtensionDataCompilerInterface(FMutableGraphGenerationContext& InGenerationContext);

	/**
	 * Register a new Extension Data compile-time constant that will be streamed in on demand.
	 * 
	 * This constant will be cooked out to its own package and will be loaded as needed.
	 * 
	 * The provided FInstancedStruct will be visible to the garbage collector, so any object
	 * references from UPROPERTYs in the instanced struct will be treated as hard references from
	 * the Customizable Object.
	 *
	 * Don't save any references to objects under the container as string paths, e.g. using
	 * TSoftObjectPtr, because the container will be moved to a different package during cooking
	 * and your paths won't be automatically updated.
	 */
	UE_API TSharedPtr<const UE::Mutable::Private::FExtensionData> MakeStreamedExtensionData(FInstancedStruct&& Data);

	/**
	 * Register a new Extension Data compile-time constant that will always be loaded.
	 *
	 * This constant will be embedded in the Customizable Object and hence will be loaded in memory
	 * as long as the Customizable Object is loaded.
	 * 
	 * Any UObjects referenced by the provided instanced struct that aren't in an asset package
	 * should be created with the return value of GetOuterForAlwaysLoadedObjects as their Outer.
	 *
	 * Move the constant data into this function and use the resulting UE::Mutable::Private::FExtensionData in the
	 * UE::Mutable::Private::Node graph, e.g. set it as the value of a UE::Mutable::Private::NodeExtensionDataConstant.
	 * 
	 * As with MakeStreamedExtensionData, the provided instanced struct will be visible to the GC.
	 */
	UE_API TSharedPtr<const UE::Mutable::Private::FExtensionData> MakeAlwaysLoadedExtensionData(FInstancedStruct&& Data);

	/** The Outer to use for objects owned by always-loaded Extension Data constants. */
	UE_API const UObject* GetOuterForAlwaysLoadedObjects();

	/**
	* Adds a node to the Generation Context list of generated nodes. This function is meant to be called
	* from classes that implement ICustomizableObjectExtensionNode::GenerateMutableNode for any generated nodes
	* so they are registered against the Mutable compiler
	*/
	UE_API void AddGeneratedNode(const class UCustomizableObjectNode* InNode);

	/** Adds a compiler log message to be displayed at the end of the compilation process */
	UE_API void CompilerLog(const FText& InLogText, const class UCustomizableObjectNode* InNode);

	FMutableGraphGenerationContext& GenerationContext;
};

#undef UE_API
