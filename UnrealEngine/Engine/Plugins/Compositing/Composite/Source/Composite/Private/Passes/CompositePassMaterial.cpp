// Copyright Epic Games, Inc. All Rights Reserved.

#include "Passes/CompositePassMaterial.h"

#include "Materials/MaterialInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessMaterialInputs.h"

DECLARE_GPU_STAT_NAMED(FCompositeMaterial, TEXT("Composite.Material"));

class FCompositePassMaterialProxy : public FCompositeCorePassProxy
{
public:
	IMPLEMENT_COMPOSITE_PASS(FCompositePassMaterialProxy);

	using FCompositeCorePassProxy::FCompositeCorePassProxy;

	/* Post-process material weak pointer. */
	TWeakObjectPtr<UMaterialInterface> Material;

	UE::CompositeCore::FPassTexture Add(FRDGBuilder& GraphBuilder, const FSceneView& InView, const UE::CompositeCore::FPassInputArray& Inputs, const UE::CompositeCore::FPassContext& PassContext) const override
	{
		RDG_EVENT_SCOPE_STAT(GraphBuilder, FCompositeMaterial, "Composite.Material");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FCompositeMaterial);

		check(ValidateInputs(Inputs));

		const UE::CompositeCore::FResourceMetadata& Metadata = Inputs[0].Metadata;
		FPostProcessMaterialInputs PostInputs = Inputs.ToPostProcessInputs(GraphBuilder, PassContext.SceneTextures);
		// Specify the scene color output format in case a render target needs to be created and our input is render target incompatible.
		PostInputs.OutputFormat = FSceneTexturesConfig::Get().ColorFormat;

		constexpr bool bEvenIfPendingKill = false;
		constexpr bool bThreadsafeTest = true;
		if (Material.IsValid(bEvenIfPendingKill, bThreadsafeTest))
		{
			FScreenPassTexture Output = AddPostProcessMaterialPass(GraphBuilder, InView, PostInputs, Material.GetEvenIfUnreachable());

			return UE::CompositeCore::FPassTexture{ MoveTemp(Output), Metadata };
		}

		return UE::CompositeCore::FPassTexture{ PostInputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder), Metadata };
	}
};


UCompositePassMaterial::UCompositePassMaterial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UCompositePassMaterial::~UCompositePassMaterial() = default;

bool UCompositePassMaterial::IsActive() const
{
	return Super::IsActive() && IsValid(PostProcessMaterial) && PostProcessMaterial->IsPostProcessMaterial();
}

bool UCompositePassMaterial::GetProxy(const UE::CompositeCore::FPassInputDecl& InputDecl, FSceneRenderingBulkObjectAllocator& InFrameAllocator, FCompositeCorePassProxy*& OutProxy) const
{
	if (IsActive())
	{
		FCompositePassMaterialProxy* Proxy = InFrameAllocator.Create<FCompositePassMaterialProxy>(UE::CompositeCore::FPassInputDeclArray{ InputDecl });
		Proxy->Material = PostProcessMaterial;
		
		OutProxy = Proxy;
		return true;
	}

	return false;
}

bool UCompositePassMaterial::NeedsSceneTextures() const
{
	// We conservatively mark any post-processing materials as potentially using scene textures.
	// TBD if material inputs could be individually checked instead.
	return true;
}

