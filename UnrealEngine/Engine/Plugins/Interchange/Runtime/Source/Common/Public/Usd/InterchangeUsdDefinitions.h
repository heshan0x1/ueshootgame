// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InterchangeUsdDefinitions.generated.h"

UENUM(BlueprintType)
enum class EInterchangeUsdPrimvar : uint8
{
	/** Store only the standard primvars such as UVs, VertexColors, etc.*/
	Standard = 0,

	/** Store only primvars in the Mesh Description used for baking to textures (basically <geompropvalue> node from MaterialX shadergraphs that are converted to <image>)*/
	Bake,

	/** Store all primvars in the MeshDescription */
	All
};

namespace UE::Interchange::USD
{
	const FString USDContextTag = TEXT("USD");

	// Name of a custom attribute added to translated nodes to contain their geometry purpose (proxy, render, guide, etc.)
	const FString GeometryPurposeIdentifier = TEXT("USD_Geometry_Purpose");

	// Prefixes we use to stash some primvar mapping information as custom attributes on mesh / material nodes, so that
	// the USD Pipeline can produce primvar-compatible materials
	const FString PrimvarUVIndexAttributePrefix = TEXT("USD_PrimvarUVIndex_");
	const FString ParameterToPrimvarAttributePrefix = TEXT("USD_ParameterPrimvar_");

	// Additional suffix we add to the UID of all primvar-compatible materials
	const FString CompatibleMaterialUidSuffix = TEXT("_USD_CompatibleMaterial_");

	// Some tokens we add to the material parameter name for USD material nodes. Put here because we need to use the
	// same tokens on the translator and the USD Pipeline, when computing primvar-compatible materials
	const FString UseTextureParameterPrefix = TEXT("Use");
	const FString UseTextureParameterSuffix = TEXT("Texture");
	const FString UVIndexParameterSuffix = TEXT("UVIndex");

	// In order to make it easy to find the skeleton root bone scene node UID from a prim path, for the root bone in particular
	// we always use an UID based on <skeleton prim path>/RootBoneUidSuffix (e.g. "\Bone\/MySkelRoot/MySkeleton/Root")
	//
	// Note that this is unrelated to the bone's *name*, which is still named after the actual USD bone name
	const FString RootBoneUidSuffix = TEXT("Root");

	// Prefix we add to the scene nodes UIDs we artificially produce for skeleton joints, to make sure we don't accidentally
	// produce an UID that is already used by a regular prim in the stage
	const FString BonePrefix = TEXT("\\Bone\\");

	// Flag indicating whether we should parse a UInterchangeShaderGraphNode on the InterchangeUSDPipeline.
	// This is now only used for the compatible primvar code
	const FString ParseMaterialIdentifier = TEXT("USD_MI_ParseMaterial");

	// Used for volumetric material parameters, whenever we assign an SVT to a material as a fallback due
	// to it's field name only
	const FString VolumeFieldNameMaterialParameterPrefix = TEXT("USD_FieldName_");

	// The USD Pipeline moves its SubdivisionLevel property value onto its Mesh factory nodes under this payload
	// attribute. The USD translator then retrieves this there and uses that subdivision level with OpenSubdiv for
	// the actual subdivision
	const FString SubdivisionLevelAttributeKey = TEXT("USD_SubdivisionLevel");

	// Custom attribute keys used to describe SVT info extracted from the USD custom schemas.
	// We add these to the Volume nodes that the USD translator emits.
	namespace SparseVolumeTexture
	{
		const FString AttributesAFormat = TEXT("USD_AttributesA_Format");
		const FString AttributesBFormat = TEXT("USD_AttributesB_Format");

		const FString AttributesAChannelR = TEXT("USD_AttributesA_R");
		const FString AttributesAChannelG = TEXT("USD_AttributesA_G");
		const FString AttributesAChannelB = TEXT("USD_AttributesA_B");
		const FString AttributesAChannelA = TEXT("USD_AttributesA_A");
		const FString AttributesBChannelR = TEXT("USD_AttributesB_R");
		const FString AttributesBChannelG = TEXT("USD_AttributesB_G");
		const FString AttributesBChannelB = TEXT("USD_AttributesB_B");
		const FString AttributesBChannelA = TEXT("USD_AttributesB_A");
	}

	namespace Primvar
	{
		const FString Number = TEXT("USD_PrimvarNumber");
		// In case of "Number" primvars, the user should concatenate the Index to this attribute
		const FString Name = TEXT("USD_PrimvarName");
		const FString TangentSpace = TEXT("USD_PrimvarTangentSpace");
		// Attribute that informs about the UID of a ShaderNode of type TextureSample
		const FString ShaderNodeTextureSample = TEXT("USD_ShaderNodeTextureSample");
		// Attribute that informs about the UID of a ShaderNode of type SparseVolumeTextureSample
		const FString ShaderNodeSparseVolumeTextureSample = TEXT("USD_ShaderNodeSparseVolumeTextureSample");
		// Attribute that informs how we should handle primvars on MeshDescriptions
		const FString Import = TEXT("USD_Import_Primvars");
	}

	// Append a case sensistive hash of NodeUid to the NodeUid itself, making it so that any TMap<FString, T> that stores
	// these IDs behaves in a case sensitive manner.
	//
	// This is needed because unfortunately the default TMap<FString, T> is not case sensitive on the key FStrings, while prim
	// names are case sensitive. Even if we could modify the UInterchangeBaseNodeContainer::Nodes map to be case sensitive, we'd
	// still constantly get issues as any other TMap<FString, T> in the codebase that stores NodeUids would show collisions
	inline FString MakeNodeUid(const FString& NodeUid)
	{
		return NodeUid + TEXT("_") + LexToString(FCrc::StrCrc32(*NodeUid));
	}

	inline FString MakeBoneNodeUid(const FString& SkeletonPrimPath, const FString& ConcatBonePath)
	{
		return MakeNodeUid(BonePrefix + SkeletonPrimPath + TEXT("/") + ConcatBonePath);
	}

	inline FString MakeRootBoneNodeUid(const FString& SkeletonPrimPath)
	{
		return MakeBoneNodeUid(SkeletonPrimPath, RootBoneUidSuffix);
	}
}	 // namespace UE::Interchange::USD
