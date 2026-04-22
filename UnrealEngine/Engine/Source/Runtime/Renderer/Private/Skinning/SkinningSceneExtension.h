// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpanAllocator.h"
#include "Containers/Map.h"
#include "Containers/Ticker.h"
#include "SceneExtensions.h"
#include "Skinning/SkinningTransformProvider.h"
#include "SkinningDefinitions.h"
#include "RendererPrivateUtils.h"
#include "InstanceCulling/InstanceCullingManager.h"
#include "Matrix3x4.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"

class FSkinningSceneParameters;

class FSkinnedSceneProxy;
class FSkinningSceneExtension : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FSkinningSceneExtension);

public:
	class FUpdater : public ISceneExtensionUpdater
	{
		DECLARE_SCENE_EXTENSION_UPDATER(FUpdater, FSkinningSceneExtension);

	public:
		FUpdater(FSkinningSceneExtension& InSceneData);

		virtual void End();
		virtual void PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms) override;
		virtual void PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet) override;
		
		void PostMeshUpdate(FRDGBuilder& GraphBuilder, const TConstArrayView<FPrimitiveSceneInfo*>& SceneInfosWithStaticDrawListUpdate);

	private:
		FSkinningSceneExtension* SceneData = nullptr;
		TConstArrayView<FPrimitiveSceneInfo*> AddedList;
		TConstArrayView<FPrimitiveSceneInfo*> UpdateList;
		TArray<int32, FSceneRenderingArrayAllocator> DirtyPrimitiveList;
		const bool bEnableAsync = true;
		bool bForceFullUpload = false;
		bool bDefragging = false;
	};

	class FRenderer : public ISceneExtensionRenderer
	{
		DECLARE_SCENE_EXTENSION_RENDERER(FRenderer, FSkinningSceneExtension);
	
	public:
		FRenderer(FSceneRendererBase& InSceneRenderer, FSkinningSceneExtension& InSceneData) : ISceneExtensionRenderer(InSceneRenderer), SceneData(&InSceneData) {}

		virtual void UpdateViewData(FRDGBuilder& GraphBuilder, const FRendererViewDataManager& ViewDataManager) override;

		virtual void UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& Buffer) override;

	private:
		FSkinningSceneExtension* SceneData = nullptr;
	};

	friend class FUpdater;

	static bool ShouldCreateExtension(FScene& InScene);

	explicit FSkinningSceneExtension(FScene& InScene);
	virtual ~FSkinningSceneExtension();

	virtual void InitExtension(FScene& InScene) override;

	virtual ISceneExtensionUpdater* CreateUpdater() override;
	virtual ISceneExtensionRenderer* CreateRenderer(FSceneRendererBase& InSceneRenderer, const FEngineShowFlags& EngineShowFlags) override;

	RENDERER_API void GetSkinnedPrimitives(TArray<FPrimitiveSceneInfo*>& OutPrimitives) const;

	RENDERER_API static const FSkinningTransformProvider::FProviderId& GetRefPoseProviderId();
	RENDERER_API static const FSkinningTransformProvider::FProviderId& GetAnimRuntimeProviderId();

private:
	enum ETask : uint32
	{
		FreeBufferSpaceTask,
		InitHeaderDataTask,
		AllocBufferSpaceTask,
		UploadHeaderDataTask,
		UploadHierarchyDataTask,
		UploadTransformDataTask,

		NumTasks
	};

	struct FHeaderData
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo	= nullptr;
		FSkinningSceneExtensionProxy* Proxy      = nullptr;
		FGuid ProviderId;
		uint32 InstanceSceneDataOffset			= 0;
		uint32 NumInstanceSceneDataEntries		= 0;
		uint32 ObjectSpaceBufferOffset			= INDEX_NONE;
		uint32 ObjectSpaceBufferCount			= 0;
		uint32 HierarchyBufferOffset			= INDEX_NONE;
		uint32 HierarchyBufferCount				= 0;
		uint32 TransformBufferOffset			= INDEX_NONE;
		uint32 TransformBufferCount				= 0;
		uint16 MaxTransformCount				= 0;
		uint16 MaxHierarchyCount				= 0;
		uint16 MaxObjectSpaceCount				= 0;
		uint8  MaxInfluenceCount				= 0;
		uint8  UniqueAnimationCount				= 1;
		uint8  bHasScale : 1					= false;
		uint8  bIsBatched : 1					= false;

		FSkinningHeader Pack() const
		{
			// Verify that values all fit within the encoded range prior to packing
			check(
				HierarchyBufferOffset	<= SKINNING_BUFFER_OFFSET_MAX &&
				TransformBufferOffset	<= SKINNING_BUFFER_OFFSET_MAX &&
				(ObjectSpaceBufferOffset == INDEX_NONE || ObjectSpaceBufferOffset <= SKINNING_BUFFER_OFFSET_MAX) &&
				MaxInfluenceCount		<= SKINNING_BUFFER_INFLUENCE_MAX
			);

			FSkinningHeader Output;
			Output.HierarchyBufferOffset	= HierarchyBufferOffset;
			Output.TransformBufferOffset	= TransformBufferOffset;
			Output.ObjectSpaceBufferOffset	= ObjectSpaceBufferOffset != INDEX_NONE ? ObjectSpaceBufferOffset : 0;
			Output.MaxTransformCount		= MaxTransformCount;
			Output.MaxInfluenceCount		= MaxInfluenceCount;
			Output.UniqueAnimationCount		= UniqueAnimationCount;
			Output.bHasScale				= bHasScale;
			return Output;
		}
	};

	class FBuffers
	{
	public:
		FBuffers();

		TPersistentByteAddressBuffer<FSkinningHeader> HeaderDataBuffer;
		TPersistentByteAddressBuffer<uint32> BoneHierarchyBuffer;
		TPersistentByteAddressBuffer<float> BoneObjectSpaceBuffer;
		TPersistentByteAddressBuffer<FCompressedBoneTransform> TransformDataBuffer;
	};
	
	class FUploader
	{
	public:
		TByteAddressBufferScatterUploader<FSkinningHeader> HeaderDataUploader;
		TByteAddressBufferScatterUploader<uint32> BoneHierarchyUploader;
		TByteAddressBufferScatterUploader<float> BoneObjectSpaceUploader;
		TByteAddressBufferScatterUploader<FCompressedBoneTransform> TransformDataUploader;
	};
	
	bool IsEnabled() const { return Buffers.IsValid(); }
	void SetEnabled(bool bEnabled);
	void SyncAllTasks() const { UE::Tasks::Wait(TaskHandles); }

	void FinishSkinningBufferUpload(
		FRDGBuilder& GraphBuilder,
		FSkinningSceneParameters* OutParams = nullptr
	);

	void PerformSkinning(
		FSkinningSceneParameters& Parameters,
		FRDGBuilder& GraphBuilder
	);

	bool ProcessBufferDefragmentation();

	UWorld* GetWorld() const;

	// Wait for tasks that modify HeaderData - after this the size and main fields do not change.
	void WaitForHeaderDataUpdateTasks() const;

private:
	FSpanAllocator ObjectSpaceAllocator;
	FSpanAllocator HierarchyAllocator;
	FSpanAllocator TransformAllocator;
	TSparseArray<FHeaderData> HeaderData;
	TSet<int32> HeaderDataIndices;
	TMap<FSkeletonBatchKey, FHeaderData> BatchHeaderData;
	TUniquePtr<FBuffers> Buffers;
	TUniquePtr<FUploader> Uploader;
	TStaticArray<UE::Tasks::FTask, NumTasks> TaskHandles;

	bool Tick(float DeltaTime);

	struct FTickState : public FRefCountBase
	{
		float DeltaTime = 0.0f;
		FVector CameraLocation = FVector::ZeroVector;
	};

	TRefCountPtr<FTickState> TickState{ new FTickState };
	FTSTicker::FDelegateHandle UpdateTimerHandle;

	TWeakObjectPtr<UWorld> WorldRef;

public:
	RENDERER_API static void ProvideRefPoseTransforms(FSkinningTransformProvider::FProviderContext& Context);
	RENDERER_API static void ProvideAnimRuntimeTransforms(FSkinningTransformProvider::FProviderContext& Context);
};
