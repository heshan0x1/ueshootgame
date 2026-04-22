// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layers/CompositeLayerBase.h"

#include "CompositeActor.h"
#include "CompositeRenderTargetPool.h"

UCompositeLayerBase::UCompositeLayerBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Ensure instances are always transactional
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SetFlags(RF_Transactional);
	}

#if WITH_EDITOR
	Name = GetClass()->GetName();
	
	const int32 NameNumber = GetFName().GetNumber();
	if (NameNumber > 1)
	{
		Name.AppendInt(NameNumber);
	}
#endif
}

UCompositeLayerBase::~UCompositeLayerBase() = default;


#if WITH_EDITOR
bool UCompositeLayerBase::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);

	if (bIsEditable && InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, Operation))
		{
			bIsEditable = !bIsSolo;
		}
	}

	return bIsEditable;
}

void UCompositeLayerBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ThisClass, bIsSolo))
	{
		if (const ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>())
		{
			for (const TObjectPtr<UCompositeLayerBase>& CompositeLayer : CompositeActor->GetCompositeLayers())
			{
				if (IsValid(CompositeLayer) && CompositeLayer != this)
				{
					CompositeLayer->Modify();
					CompositeLayer->bIsSolo = false;
				}
			}
		}
	}
}
#endif //WITH_EDITOR

bool UCompositeLayerBase::IsEnabled() const
{
	return bIsEnabled;
}

void UCompositeLayerBase::SetEnabled(bool bInEnabled)
{
	bIsEnabled = bInEnabled;
}

void UCompositeLayerBase::AddChildPasses(UE::CompositeCore::FPassInputDecl& InBasePassInput, FTraversalContext& InContext, FSceneRenderingBulkObjectAllocator& InFrameAllocator, TArrayView<const TObjectPtr<UCompositePassBase>> InPasses) const
{
	// Iterate backwards so that lower passes in the UI are executed first
	// TODO: Flip back once we have UI customization displaying passes in the correct bottom-up order
	for (int32 PassIndex = InPasses.Num() - 1; PassIndex >= 0; --PassIndex)
	{
		const TObjectPtr<UCompositePassBase>& SubPass = InPasses[PassIndex];

		if (IsValid(SubPass) && SubPass->IsActive())
		{
			FCompositeCorePassProxy* ChildPassProxy;
			if (SubPass->GetProxy(InBasePassInput, InFrameAllocator, ChildPassProxy))
			{
				// Next pass should receive the output of the current child pass.
				InBasePassInput.Set<const FCompositeCorePassProxy*>(ChildPassProxy);

				if (SubPass->NeedsSceneTextures())
				{
					InContext.bNeedsSceneTextures = true;
				}
			}
		}
	}
}

const FIntPoint UCompositeLayerBase::GetRenderResolution() const
{
	const ACompositeActor* CompositeActor = GetTypedOuter<ACompositeActor>();
	
	if (IsValid(CompositeActor))
	{
		return CompositeActor->RenderResolution;
	}

	return FCompositeRenderTargetPool::DefaultSize;
}

ECompositeCoreMergeOp UCompositeLayerBase::GetMergeOperation(const FTraversalContext& InContext) const
{
	return InContext.bIsSolo ? ECompositeCoreMergeOp::None : Operation;
}

UE::CompositeCore::FPassInputDecl UCompositeLayerBase::GetDefaultSecondInput(const FTraversalContext& InContext) const
{
	using namespace UE::CompositeCore;

	UE::CompositeCore::FPassInputDecl InputDecl;

	if (InContext.bIsFirstPass)
	{
		// Force an empty previous texture on the first pass
		FPassExternalResourceDesc Desc1;
		Desc1.Id = BUILT_IN_EMPTY_ID;
		InputDecl.Set<FPassExternalResourceDesc>(Desc1);
	}
	else
	{
		InputDecl.Set<FPassInternalResourceDesc>({});
	}

	return InputDecl;
}

UE::CompositeCore::ResourceId UCompositeLayerBase::FTraversalContext::FindOrCreateExternalTexture(TWeakObjectPtr<UTexture> InTexture, UE::CompositeCore::FResourceMetadata InMetadata)
{
	int32 TextureIndex = ExternalTextures.IndexOfByPredicate([&InTexture, &InMetadata](const UE::CompositeCore::FExternalTexture& InExternalTexture)
		{
			return (InExternalTexture.Texture.Get() == InTexture.Get()) && (InExternalTexture.Metadata == InMetadata);
		}
	);

	if (TextureIndex == INDEX_NONE)
	{
		ExternalTextures.Add(UE::CompositeCore::FExternalTexture{ MoveTemp(InTexture), MoveTemp(InMetadata) });
		
		TextureIndex = ExternalTextures.Num() - 1;
	}

	return UE::CompositeCore::EXTERNAL_RANGE_START_ID + TextureIndex;
}

const TArray<UE::CompositeCore::FExternalTexture>& UCompositeLayerBase::FTraversalContext::GetExternalTextures() const
{
	return ExternalTextures;
}

