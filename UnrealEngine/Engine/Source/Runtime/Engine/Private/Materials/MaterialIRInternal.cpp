// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialIREmitter.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Engine/Texture.h"
#include "VT/RuntimeVirtualTexture.h"

#if WITH_EDITOR

namespace MIR::Internal {

EMaterialValueType GetTextureMaterialValueType(const UObject* TextureObject)
{
	if (TextureObject)
	{
		if (TextureObject->IsA<UTexture>())
		{
			return Cast<UTexture>(TextureObject)->GetMaterialType();
		}
		if (TextureObject->IsA<URuntimeVirtualTexture>())
		{
			return MCT_TextureVirtual;
		}
	}
	return MCT_Unknown;
}

EMaterialTextureParameterType TextureMaterialValueTypeToParameterType(EMaterialValueType Type)
{
	switch (Type)
	{
		case MCT_Texture2D: return EMaterialTextureParameterType::Standard2D;
		case MCT_Texture2DArray: return EMaterialTextureParameterType::Array2D;
		case MCT_TextureCube: return EMaterialTextureParameterType::Cube;
		case MCT_TextureCubeArray: return EMaterialTextureParameterType::ArrayCube;
		case MCT_VolumeTexture: return EMaterialTextureParameterType::Volume;
		case MCT_TextureVirtual: return EMaterialTextureParameterType::Virtual;
		default: UE_MIR_UNREACHABLE();
	}
}

uint32 HashBytes(const void* InPtr, uint32 Size)
{
	check((uintptr_t)InPtr % 4 == 0); // Require the pointer to be 4-byte aligned

	uint32 Hash = 0;

	const uint32* WordPtr = (const uint32*)InPtr;
	uint32 NumWords = Size / 4;
	for (uint32 i = 0; i < NumWords; ++i)
	{
		Hash = HashCombineFast(Hash, WordPtr[i]);
	}

	const uint8* EndPtr = (const uint8*)(WordPtr + NumWords);

	uint32 End = 0;
	switch (Size % 4)
	{
		case 3: End |= uint32(EndPtr[2]) << 16; // fallthrough
		case 2: End |= uint32(EndPtr[1]) << 8;  // fallthrough
		case 1: End |= uint32(EndPtr[0]);       // fallthrough
			Hash = HashCombineFast(Hash, End);
		default: break;
	}

	return Hash;
}

} // namespace MIR::Internal

#endif
