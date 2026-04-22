// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/PropertyBag.h"

#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanPaletteItemPath.h"
#include "MetaHumanPipelineSlotSelection.h"

#include "MetaHumanCollectionBlueprintLibrary.generated.h"

/**
 * @brief Exposes blueprint functions to operate on FMetaHumanPaletteItemKey.
 *
 * The idea is that functions should be tagged with ScriptMethod so users can call it directly
 * on the struct without the need to reference the library
 */
UCLASS()
class UMetaHumanPaletteKeyBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * @brief Returns true if the other key is identical to this one except for Variation
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Key", meta = (ScriptMethod))
	static bool ReferencesSameAsset(const FMetaHumanPaletteItemKey& InKey, const FMetaHumanPaletteItemKey& InOther);

	/**
	 * @brief Produces a string suitable for using as part of an asset name
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Key", meta = (ScriptMethod))
	static FString ToAssetNameString(const FMetaHumanPaletteItemKey& InKey);
};

UCLASS()
class UMetaHumanPaletteItemPathBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * @brief Constructs a FMetaHumanPaletteItemPath from a FMetaHumanPaletteItemKey.
	 * 
	 * Exposed as a Make function for FMetaHumanPaletteItemPath
	 */
	UFUNCTION(BlueprintCallable, Category = "Item Path", meta = (NativeMakeFunc))
	static FMetaHumanPaletteItemPath MakeItemPath(const FMetaHumanPaletteItemKey& InItemKey);
};

/**
 * The types of parameters supported by Wardrobe Items
 * These map directly EPropertyBagPropertyType but since not all values
 * are exposed (ex Struct is hidden) create a new enum to map the
 * types from property bag to this enum.
 */
UENUM()
enum class EMetaHumanCharacterInstanceParameterType : uint8
{
	None = (uint8) EPropertyBagPropertyType::None	UMETA(Hidden),
	Bool = (uint8) EPropertyBagPropertyType::Bool,
	Float = (uint8) EPropertyBagPropertyType::Float,
	Color = (uint8) EPropertyBagPropertyType::Struct,
};

/**
 * @brief Struct that represents a parameter of a particular item in a collection
 * 
 * This struct is designed to work in scripting environments and will have functions to
 * get and set values hoisted from UMetaHumanCharacterInstanceParameterBlueprintLibrary
 * 
 * Note that calling one of the Set functions automatically update the parameter value
 * for the item it represents in the instance that was used when querying them.
 */
USTRUCT(BlueprintType)
struct FMetaHumanCharacterInstanceParameter
{
	GENERATED_BODY()

	// The name of the parameter
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameters")
	FName Name;

	// The type of the parameter. Can be used to check which of Set/Get functions to call
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameters")
	EMetaHumanCharacterInstanceParameterType Type = EMetaHumanCharacterInstanceParameterType::None;

	// The item path to locate this parameter in the instance
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameters")
	FMetaHumanPaletteItemPath ItemPath;

	// Pointer back to the character instance this parameter represents
	UPROPERTY()
	TWeakObjectPtr<class UMetaHumanCharacterInstance> Instance;
};

/**
 * @brief Exposes blueprint functions to operate on FMetaHumanCharacterInstanceParameter
 */
UCLASS()
class UMetaHumanCharacterInstanceParameterBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool GetBool(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool& OutValue);

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool SetBool(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool InValue);

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool GetFloat(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float& OutValue);

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool SetFloat(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float InValue);

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool GetColor(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FLinearColor& OutValue);

	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static bool SetColor(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FLinearColor& InValue);
};

/**
 * @brief Exposes blueprint functions to operate on FMetaHumanPipelineSlotSelection.
 */
UCLASS()
class UMetaHumanPipelineSlotSelectionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Pipeline", meta = (ScriptMethod))
	static FMetaHumanPaletteItemPath GetSelectedItemPath(const FMetaHumanPipelineSlotSelection& InSlotSelection);
};

/**
 * @brief Exposes blueprint functions to operate on UMetaHumanCharacterInstance
 * 
 * This can be used to add functions that are only needed for the scripting interface.
 * The main use right now is to provide a way to get and set instance parameters since
 * Property Bags are not directly exposed for use in blueprints
 */
UCLASS()
class UMetaHumanCharacterInstanceBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * @brief Gets the list of all instance parameters for a given item
	 */
	UFUNCTION(BlueprintCallable, Category = "Parameters", meta = (ScriptMethod))
	static TArray<FMetaHumanCharacterInstanceParameter> GetInstanceParameters(class UMetaHumanCharacterInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath);
};