// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MemoryCounters.h"
#include "MuR/Registry.h"
#include "MuR/Operations.h"


namespace UE::Mutable::Private
{
	// Mesh
	struct FGeneratedMeshKey
	{
		OP::ADDRESS Address = 0;
		
		TMemoryTrackedArray<uint8> ParameterValuesBlob;
		
		bool operator==(const FGeneratedMeshKey&) const = default;
	};

	struct FGeneratedMeshData
	{
	};
	
	MUTABLERUNTIME_API uint32 GetTypeHash(const FGeneratedMeshKey& Key);
	
	typedef TRegistry<FGeneratedMeshKey, FGeneratedMeshData>::FHandle FMeshId;

	typedef TRegistry<FGeneratedMeshKey, FGeneratedMeshData> FMeshIdRegistry;

	MUTABLERUNTIME_API uint32 GetTypeHashPersistent(const FMeshId& Key);

	// Image
	struct FGeneratedImageKey
	{
		OP::ADDRESS Address = 0;

		TMemoryTrackedArray<uint8> ParameterValuesBlob;

		bool operator==(const FGeneratedImageKey&) const = default;
	};

	
	struct FGeneratedImageData
	{
	};
	
	MUTABLERUNTIME_API uint32 GetTypeHash(const FGeneratedImageKey& Key);

	typedef TRegistry<FGeneratedImageKey, FGeneratedImageData>::FHandle FImageId;

	typedef TRegistry<FGeneratedImageKey, FGeneratedImageData> FImageIdRegistry;

	MUTABLERUNTIME_API uint32 GetTypeHashPersistent(const FImageId& Key);

	// Material
	struct FGeneratedMaterialKey
	{
		OP::ADDRESS Address = 0;

		TMemoryTrackedArray<uint8> ParameterValuesBlob;

		bool operator==(const FGeneratedMaterialKey&) const = default;
	};

	
	struct FGeneratedMaterialData
	{
	};
	
	MUTABLERUNTIME_API uint32 GetTypeHash(const FGeneratedMaterialKey& Key);
	
	typedef TRegistry<FGeneratedMaterialKey, FGeneratedMaterialData>::FHandle FMaterialId;

	typedef TRegistry<FGeneratedMaterialKey, FGeneratedMaterialData> FMaterialIdRegistry;

	MUTABLERUNTIME_API uint32 GetTypeHashPersistent(const FMaterialId& Key);
}
