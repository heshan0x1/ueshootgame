// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpanAllocator.h"
#include "Containers/Map.h"
#include "SceneExtensions.h"
#include "RendererPrivateUtils.h"
#include "Matrix3x4.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"
#include "GameTime.h"

#define ENABLE_SKELETON_DEBUG_NAME (UE_BUILD_DEBUG | UE_BUILD_DEVELOPMENT)

struct FSkeletonBatch
{
#if ENABLE_SKELETON_DEBUG_NAME
	FName SkeletonName = TEXT("Invalid");
#endif
	FGuid SkeletonGuid;
	uint32 MaxBoneTransforms = 0u;
	uint32 UniqueAnimationCount = 0u;
};

struct FSkeletonBatchKey
{
#if ENABLE_SKELETON_DEBUG_NAME
	FName SkeletonName = TEXT("Invalid");
#endif
	FGuid SkeletonGuid;
	FGuid TransformProviderId;

	bool operator == (const FSkeletonBatchKey& InOther) const
	{
		return SkeletonGuid == InOther.SkeletonGuid && TransformProviderId == InOther.TransformProviderId;
	}

	bool operator != (const FSkeletonBatchKey& InOther) const
	{
		return !(*this == InOther);
	}

	friend inline SIZE_T GetTypeHash(const FSkeletonBatchKey& InKey)
	{
		return HashCombine(GetTypeHash(InKey.SkeletonGuid), GetTypeHash(InKey.TransformProviderId));
	}
};

class FSkinningSceneExtensionProxy;

class FSkinningTransformProvider : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FSkinningTransformProvider);

public:
	typedef FGuid FProviderId;

	struct FProviderRange
	{
		FProviderId Id;
		uint32 Count;
		uint32 Offset;
	};

	struct FProviderIndirection
	{
		FProviderIndirection(uint32 InIndex, uint32 InTransformOffset, uint32 InHierarchyOffset)
			: Index(InIndex)
			, TransformOffset(InTransformOffset)
			, HierarchyOffset(InHierarchyOffset)
		{}

		uint32 Index = 0;
		uint32 TransformOffset = 0;
		uint32 HierarchyOffset = 0;
	};

	struct FProviderContext
	{
		FProviderContext(
			const TConstArrayView<FPrimitiveSceneInfo*> InPrimitives,
			const TConstArrayView<FSkinningSceneExtensionProxy*> InProxies,
			const TConstArrayView<FProviderIndirection> InIndirections,
			const TConstArrayView<FSkeletonBatch> InSkeletonBatches,
			float InDeltaTime,
			FRDGBuilder& InGraphBuilder,
			FRDGBufferRef InTransformBuffer,
			FRDGBufferSRVRef InHierarchyBufferSRV
		)
		: Primitives(InPrimitives)
		, Proxies(InProxies)
		, Indirections(InIndirections)
		, SkeletonBatches(InSkeletonBatches)
		, GraphBuilder(InGraphBuilder)
		, TransformBuffer(InTransformBuffer)
		, HierarchyBufferSRV(InHierarchyBufferSRV)
		, DeltaTime(InDeltaTime)
		{
		}

		TConstArrayView<FPrimitiveSceneInfo*> Primitives;
		TConstArrayView<FSkinningSceneExtensionProxy*> Proxies;
		TConstArrayView<FProviderIndirection> Indirections;
		TConstArrayView<FSkeletonBatch> SkeletonBatches;

		FRDGBuilder& GraphBuilder;
		FRDGBufferRef TransformBuffer;
		FRDGBufferSRVRef HierarchyBufferSRV;

		float DeltaTime = 0.0f;
	};

	DECLARE_DELEGATE_OneParam(FOnProvideTransforms, FProviderContext&);

public:
	using ISceneExtension::ISceneExtension;

	static bool ShouldCreateExtension(FScene& InScene);

	RENDERER_API void RegisterProvider(const FProviderId& Id, const FOnProvideTransforms& Delegate, bool bUsesSkeletonBatches);
	RENDERER_API void UnregisterProvider(const FProviderId& Id);

	void Broadcast(const TConstArrayView<FProviderRange> Ranges, FProviderContext& Context);

	inline bool HasProviders() const
	{
		return !Providers.IsEmpty();
	}

	inline TArray<FProviderId> GetProviderIds() const
	{
		TArray<FProviderId> Ids;
		Ids.Reserve(Providers.Num());
		for (const FTransformProvider& Provider : Providers)
		{
			Ids.Add(Provider.Id);
		}
		return Ids;
	}

	inline TArray<FProviderId> GetPrimitiveProviderIds() const
	{
		TArray<FProviderId> Ids;
		Ids.Reserve(Providers.Num());
		for (const FTransformProvider& Provider : Providers)
		{
			if (!Provider.bUsesSkeletonBatches)
			{
				Ids.Add(Provider.Id);
			}
		}
		return Ids;
	}

	inline TArray<FProviderId> GetSkeletonProviderIds() const
	{
		TArray<FProviderId> Ids;
		Ids.Reserve(Providers.Num());
		for (const FTransformProvider& Provider : Providers)
		{
			if (Provider.bUsesSkeletonBatches)
			{
				Ids.Add(Provider.Id);
			}
		}
		return Ids;
	}

private:
	struct FTransformProvider
	{
		FProviderId Id;
		FOnProvideTransforms Delegate;
		uint8 bUsesSkeletonBatches : 1 = false;
	};

	TArray<FTransformProvider> Providers;
};

RENDERER_API const FSkinningTransformProvider::FProviderId& GetRefPoseProviderId();
RENDERER_API const FSkinningTransformProvider::FProviderId& GetAnimRuntimeProviderId();
