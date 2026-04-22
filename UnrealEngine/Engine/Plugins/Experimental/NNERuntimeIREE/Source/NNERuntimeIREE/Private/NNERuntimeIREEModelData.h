// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "NNERuntimeIREECompiler.h"
#include "NNETypes.h"
#include "UObject/Class.h"

#include "NNERuntimeIREEModelData.generated.h"

/**
 * IREE model data class.
 */
UCLASS()
class UNNERuntimeIREEModelDataCPU : public UObject
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	/**
	 * Check data for matching Guid and Version without deserializing everything.
	 */
	static bool IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version);

	/**
	 * A Guid that uniquely identifies this IREE model data.
	 */
	FGuid GUID;

	/**
	 * Current version of this IREE model data.
	 */
	int32 Version;

	/**
	 * A Guid that uniquely identifies the model.
	 */
	FGuid FileId;

	/**
	 * Serialized module meta data.
	 */
	TArray64<uint8> ModuleMetaData;

	/**
	 * Serialized compiler output.
	 */
	TArray64<uint8> CompilerResult;
};

/**
 * IREE model data header RDG struct.
 */
USTRUCT()
struct FNNERuntimeIREEModelDataHeaderRDG
{
	GENERATED_BODY()

	/**
	 * A Guid that uniquely identifies this IREE model data.
	 */
	UPROPERTY()
	FGuid GUID;

	/**
	 * Current version of this IREE model data.
	 */
	UPROPERTY()
	int32 Version = 0;

	/**
	 * A Guid that uniquely identifies the model.
	 */
	UPROPERTY()
	FGuid FileId;

	/**
	 * List of shader platforms supported by this IREE model data.
	 */
	UPROPERTY()
	TArray<FString> ShaderPlatforms;
};

/**
 * IREE model data RDG class.
 */
UCLASS()
class UNNERuntimeIREEModelDataRDG : public UObject
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	static FNNERuntimeIREEModelDataHeaderRDG ReadHeader(FArchive& Ar);

	/**
	 * Model data header.
	 */
	FNNERuntimeIREEModelDataHeaderRDG Header;

	/**
	 * Serialized module meta data.
	 */
	TArray64<uint8> ModuleMetaData;

	/**
	 * Serialized compiler output.
	 */
	TArray64<uint8> CompilerResult;
};