// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"
#include "Engine/SkeletalMesh.h"
#include "HLSLTypeAliases.h"
#include "Shared/DynamicWindCommon.ush"
#include "DynamicWindParameters.h"
#include "Skinning/SkinningTransformProvider.h"
#include "SkinningDefinitions.h"
#include "Animation/TransformProviderData.h"

class USkeleton;
namespace Nanite { class FSkinnedSceneProxy; }

#define DYNAMIC_WIND_TRANSFORM_PROVIDER_GUID 0xDFD4874B, 0xEE57466D, 0x883D6419, 0xAFE99EAC
#define DYNAMIC_WIND_DEBUG_SKELETON_NAMES (UE_BUILD_DEBUG | UE_BUILD_DEVELOPMENT)

class DYNAMICWIND_API FDynamicWindTransformProvider
{
public:
	explicit FDynamicWindTransformProvider(FScene& InScene);
	FDynamicWindTransformProvider() = delete;
	~FDynamicWindTransformProvider();

	bool Register();
	bool Unregister();

	UE_DEPRECATED(5.7, "This method will be made private in the next release. It shouldn't be used externally")
	void RegisterSceneProxy(const Nanite::FSkinnedSceneProxy* InProxy);
	UE_DEPRECATED(5.7, "This method will be made private in the next release. It shouldn't be used externally")
	void UnregisterSceneProxy(const Nanite::FSkinnedSceneProxy* InProxy);

	int32 UpdateParameters(const FDynamicWindParameters& Parameters);

	float GetBlendedWindAmplitude() const;

private:
	using FBoneDataBuffer = TPersistentByteAddressBuffer<FDynamicWindBoneData>;
	using FBoneDataUploader = TByteAddressBufferScatterUploader<FDynamicWindBoneData>;

	struct FSkeletonEntry
	{	
		uint64 UserDataHash = 0;
		uint32 NumBones = 0;
		uint32 ReferenceCount = 0;
	#if DYNAMIC_WIND_DEBUG_SKELETON_NAMES
		FName SkeletonName;
	#endif
		
		FDynamicWindSkeletonData Data;
	};

	void ProvideTransforms(FSkinningTransformProvider::FProviderContext& Context);

private:
	FScene& Scene;

	TMap<FGuid, FSkeletonEntry> SkeletonLookup;
	FSpanAllocator BoneDataAllocator;
	TUniquePtr<FBoneDataUploader> BoneDataUploader;
	FBoneDataBuffer BoneDataBuffer;

	FDynamicWindParameters WindParameters = {};

	uint64 LastSimulatedFrameNumber = 0u;

	float BlendedWindAmplitude = -1.0f;

	FDelegateHandle OnProxyCreateRenderThreadResources;
	FDelegateHandle OnProxyDestroyRenderThreadResources;

	bool bIsRegistered = false;
};
