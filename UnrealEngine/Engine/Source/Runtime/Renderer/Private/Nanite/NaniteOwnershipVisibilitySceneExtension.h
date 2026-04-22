// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneExtensions.h"
#include "PrimitiveSceneInfo.h" // FPersistentPrimitiveIndex

class FRDGBuffer;

namespace Nanite
{

// This scene extension keeps track of nanite primitives that have bOwnerNoSee or bOnlyOwnerSee enabled on them.
// It then creates a bitmask GPU buffer to efficiently hide such primitives in the relevant views.
class FOwnershipVisibilitySceneExtension : public ISceneExtension
{
	DECLARE_SCENE_EXTENSION(RENDERER_API, FOwnershipVisibilitySceneExtension);
public:
	using ISceneExtension::ISceneExtension;

	static bool ShouldCreateExtension(FScene& Scene);

	//~ Begin ISceneExtension Interface.
	virtual ISceneExtensionUpdater* CreateUpdater() override;
	virtual ISceneExtensionRenderer* CreateRenderer(FSceneRendererBase& InSceneRenderer, const FEngineShowFlags& EngineShowFlags) override;
	//~ End ISceneExtension Interface.

	const TArray<FPersistentPrimitiveIndex>& GetPrimitivesWithOwnership() const;
	int32 GetMaxPersistentPrimitiveIndex() const;

private:

	// Private updater class for handling adding and removal of primitives.
	class FUpdater : public ISceneExtensionUpdater
	{
		DECLARE_SCENE_EXTENSION_UPDATER(FUpdater, FOwnershipVisibilitySceneExtension);
	public:
		FUpdater(FOwnershipVisibilitySceneExtension& InSceneExtension) : SceneExtension(InSceneExtension) {}

		//~ Begin ISceneExtensionUpdater Interface.
		virtual void PreSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePreUpdateChangeSet& ChangeSet, FSceneUniformBuffer& SceneUniforms) override;
		virtual void PostSceneUpdate(FRDGBuilder& GraphBuilder, const FScenePostUpdateChangeSet& ChangeSet) override;
		//~ End ISceneExtensionUpdater Interface.

	private:
		FOwnershipVisibilitySceneExtension& SceneExtension;
	};

	// Private "renderer" class, creating a bit array buffer with one bit per primitive per view.
	class FRenderer : public ISceneExtensionRenderer
	{
		DECLARE_SCENE_EXTENSION_RENDERER(FRenderer, FOwnershipVisibilitySceneExtension);
	public:
		FRenderer(FSceneRendererBase& InSceneRenderer, FOwnershipVisibilitySceneExtension& InSceneExtension)
			: ISceneExtensionRenderer(InSceneRenderer), SceneExtension(InSceneExtension) {
		}

		//~ Begin ISceneExtensionRenderer Interface.
		virtual void UpdateViewData(FRDGBuilder& GraphBuilder, const FRendererViewDataManager& ViewDataManager) override;
		virtual void UpdateSceneUniformBuffer(FRDGBuilder& GraphBuilder, FSceneUniformBuffer& SceneUniforms) override;
		//~ End ISceneExtensionRenderer Interface.

	private:
		FOwnershipVisibilitySceneExtension& SceneExtension;
		FRDGBuffer* OwnershipHiddenPrimitivesBitArrayBuffer = nullptr;
	};

	TArray<FPersistentPrimitiveIndex> NanitePrimitivesWithOwnership;
};

} // namespace Nanite
