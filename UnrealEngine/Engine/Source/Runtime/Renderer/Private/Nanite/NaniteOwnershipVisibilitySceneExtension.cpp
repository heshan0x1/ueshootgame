// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteOwnershipVisibilitySceneExtension.h"
#include "ScenePrivate.h"
#include "RenderUtils.h"
#include "ViewData.h"
#include "PrimitiveSceneInfo.h"
#include "RenderGraphUtils.h"


BEGIN_SHADER_PARAMETER_STRUCT(FNaniteOwnershipVisibilityParameters, RENDERER_API)
	SHADER_PARAMETER(uint32, PrimitivesPerView)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, HiddenPrimitives)
END_SHADER_PARAMETER_STRUCT()

DECLARE_SCENE_UB_STRUCT(FNaniteOwnershipVisibilityParameters, NaniteOwnershipVisibility, RENDERER_API)

namespace Nanite
{

static void GetDefaultOwnershipVisibilityParameters(FNaniteOwnershipVisibilityParameters& OutParameters, FRDGBuilder& GraphBuilder)
{
	OutParameters.PrimitivesPerView = 0;
	OutParameters.HiddenPrimitives = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4u));
}

static bool IsPrimitiveOwnershipVisibilityRelevant(const FPrimitiveSceneProxy* SceneProxy)
{
	return SceneProxy->IsNaniteMesh() && (SceneProxy->IsOwnerNoSee() || SceneProxy->IsOnlyOwnerSee());
}

IMPLEMENT_SCENE_EXTENSION(FOwnershipVisibilitySceneExtension);

bool FOwnershipVisibilitySceneExtension::ShouldCreateExtension(FScene& Scene)
{
	return DoesRuntimeSupportNanite(GetFeatureLevelShaderPlatform(Scene.GetFeatureLevel()), true, true);
}

ISceneExtensionUpdater* FOwnershipVisibilitySceneExtension::CreateUpdater()
{
	return new FUpdater(*this);
}

ISceneExtensionRenderer* FOwnershipVisibilitySceneExtension::CreateRenderer(FSceneRendererBase& InSceneRenderer, const FEngineShowFlags& EngineShowFlags)
{
	return new FRenderer(InSceneRenderer, *this);
}

const TArray<FPersistentPrimitiveIndex>& FOwnershipVisibilitySceneExtension::GetPrimitivesWithOwnership() const
{
	return NanitePrimitivesWithOwnership;
}

int32 FOwnershipVisibilitySceneExtension::GetMaxPersistentPrimitiveIndex() const
{
	return Scene.GetMaxPersistentPrimitiveIndex();
}

void FOwnershipVisibilitySceneExtension::FUpdater::PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms)
{
	for (FPrimitiveSceneInfo* PrimitiveSceneInfo : ChangeSet.RemovedPrimitiveSceneInfos)
	{
		if (IsPrimitiveOwnershipVisibilityRelevant(PrimitiveSceneInfo->Proxy))
		{
			SceneExtension.NanitePrimitivesWithOwnership.RemoveSwap(PrimitiveSceneInfo->GetPersistentIndex());
		}
	}
}

void FOwnershipVisibilitySceneExtension::FUpdater::PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet)
{
	for (FPrimitiveSceneInfo* PrimitiveSceneInfo : ChangeSet.AddedPrimitiveSceneInfos)
	{
		if (IsPrimitiveOwnershipVisibilityRelevant(PrimitiveSceneInfo->Proxy))
		{
			SceneExtension.NanitePrimitivesWithOwnership.Add(PrimitiveSceneInfo->GetPersistentIndex());
		}
	}
}

void FOwnershipVisibilitySceneExtension::FRenderer::UpdateViewData(FRDGBuilder& GraphBuilder, const FRendererViewDataManager& ViewDataManager)
{
	TConstArrayView<FViewInfo*> Views = ViewDataManager.GetRegisteredPrimaryViews();
	const int32 NumViews = Views.Num();
	TConstArrayView<FPersistentPrimitiveIndex> OwnedPrimitiveIds = SceneExtension.GetPrimitivesWithOwnership();
	const int32 NumOwnedPrimitives = OwnedPrimitiveIds.Num();

	if (NumOwnedPrimitives == 0)
	{
		OwnershipHiddenPrimitivesBitArrayBuffer = nullptr;
	}
	else
	{
		const int32 NumPrimitivesPerView = SceneExtension.GetMaxPersistentPrimitiveIndex();
		TBitArray<SceneRenderingBitArrayAllocator> BitArray(false, (uint32)NumPrimitivesPerView * (uint32)NumViews);

		const FScene* ScenePtr = GetSceneRenderer().GetScene();

		for (FPersistentPrimitiveIndex PersistentPrimitiveIndex : OwnedPrimitiveIds)
		{
			const FPrimitiveSceneProxy* SceneProxy = ScenePtr->GetPrimitiveSceneInfo(PersistentPrimitiveIndex)->Proxy;
			const bool bIsOwnerNoSee = SceneProxy->IsOwnerNoSee();
			const bool bIsOnlyOwnerSee = SceneProxy->IsOnlyOwnerSee();

			for (int32 ViewIndex = 0; ViewIndex < NumViews; ++ViewIndex)
			{
				check(ViewIndex == Views[ViewIndex]->SceneRendererPrimaryViewId); // We rely on SceneRendererPrimaryViewId being an index into RegisteredPrimaryViews on the ViewDataManager
				
				const bool bIsEditorView = Views[ViewIndex]->Family->EngineShowFlags.Editor;
				const bool bIsOwnedByView = SceneProxy->IsOwnedBy(Views[ViewIndex]->ViewActor);
				const bool bIsHidden = !bIsEditorView && ((bIsOwnedByView && bIsOwnerNoSee) || (!bIsOwnedByView && bIsOnlyOwnerSee));

				BitArray[ViewIndex * NumPrimitivesPerView + PersistentPrimitiveIndex.Index] = bIsHidden;
			}
		}

		OwnershipHiddenPrimitivesBitArrayBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Nanite.OwnershipHiddenPrimitivesBuffer"), TConstArrayView<uint32>(BitArray.GetData(), FBitSet::CalculateNumWords(BitArray.Num())));
	}
}

void FOwnershipVisibilitySceneExtension::FRenderer::UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniforms)
{
	FNaniteOwnershipVisibilityParameters Parameters;
	if (OwnershipHiddenPrimitivesBitArrayBuffer)
	{
		Parameters.PrimitivesPerView = SceneExtension.GetMaxPersistentPrimitiveIndex();
		Parameters.HiddenPrimitives = GraphBuilder.CreateSRV(OwnershipHiddenPrimitivesBitArrayBuffer);
	}
	else
	{
		Parameters.PrimitivesPerView = 0;
		Parameters.HiddenPrimitives = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, 4u));
	}
	SceneUniforms.Set(SceneUB::NaniteOwnershipVisibility, Parameters);
}

} // namespace Nanite

IMPLEMENT_SCENE_UB_STRUCT(FNaniteOwnershipVisibilityParameters, NaniteOwnershipVisibility, Nanite::GetDefaultOwnershipVisibilityParameters);
