// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageViewers/TextureSampleCache.h"

#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MediaViewerUtils.h"
#include "Misc/ScopeLock.h"
#include "PixelFormat.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

namespace UE::MediaViewer::Private
{

FTextureSampleCache::FTextureSampleCache()
{
}

FTextureSampleCache::FTextureSampleCache(TNotNull<UTexture*> InTexture, EPixelFormat InPixelFormat)
	: TextureWeak(InTexture)
	, SampleCS(FCriticalSection())
	, RenderTarget(nullptr)
{
}

bool FTextureSampleCache::IsValid() const
{
	return TextureWeak.IsValid();
}

bool FTextureSampleCache::NeedsUpdate(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime) const
{
	UTexture* Texture = TextureWeak.Get();

	if (!Texture)
	{
		return false;
	}

	constexpr int32 MinimumSurfaceSize = 3;

	if (Texture->GetSurfaceWidth() < MinimumSurfaceSize || Texture->GetSurfaceHeight() < MinimumSurfaceSize)
	{
		return false;
	}

	FScopeLock Lock(&SampleCS);
	return bDirty || !PixelColorSample.IsSet() || InPixelCoordinates != PixelColorSample->Coordinates
		|| (InTime.IsSet() && (PixelColorSample->Time != InTime.GetValue()));
}

const FLinearColor* FTextureSampleCache::GetPixelColor(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime)
{
	UTexture* Texture = TextureWeak.Get();

	if (!Texture)
	{
		return nullptr;
	}

	const bool bNeedsUpdate = NeedsUpdate(InPixelCoordinates, InTime);

	if (bNeedsUpdate)
	{
		// If we're already using a render target, we're good to directly sample it.
		if (Texture && Texture->IsA<UTextureRenderTarget2D>())
		{
			SetPixelColor_RHI(InPixelCoordinates, InTime, Texture);
		}
		else
		{
			SetPixelColor_RenderTarget(InPixelCoordinates, InTime);
		}
	}

	{
		FScopeLock Lock(&SampleCS);

		if (PixelColorSample.IsSet())
		{
			return &PixelColorSample->Color;
		}
	}

	return nullptr;
}

void FTextureSampleCache::MarkDirty()
{
	FScopeLock Lock(&SampleCS);
	bDirty = true;
}

void FTextureSampleCache::Invalidate()
{
	{
		FScopeLock Lock(&SampleCS);
		PixelColorSample.Reset();
	}
}

FTextureSampleCache& FTextureSampleCache::operator=(const FTextureSampleCache& InOther)
{
	FScopeLock MyLock(&SampleCS);
	FScopeLock OtherLock(&InOther.SampleCS);

	TextureWeak = InOther.TextureWeak;
	bDirty = InOther.bDirty;
	PixelColorSample = InOther.PixelColorSample;
	
	return *this;
}

FString FTextureSampleCache::GetReferencerName() const
{
	static const FString ReferencerName = TEXT("FTextureSampleCache");
	return ReferencerName;
}

void FTextureSampleCache::AddReferencedObjects(FReferenceCollector& InCollector)
{
	if (RenderTarget)
	{
		InCollector.AddReferencedObject(RenderTarget);
	}
}

void FTextureSampleCache::SetPixelColor_RHI(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime, UTexture* InTexture)
{
	ENQUEUE_RENDER_COMMAND(GetPixelColors)
	(
		[ThisWeak = SharedThis(this).ToWeakPtr(), TextureWeak = TWeakObjectPtr<UTexture>(InTexture), InPixelCoordinates, InTime](FRHICommandListImmediate& RHICmdList)
		{
			TSharedPtr<FTextureSampleCache> This = ThisWeak.Pin();

			if (!This.IsValid())
			{
				return;
			}

			TStrongObjectPtr<UTexture> TextureToSample = TextureWeak.Pin();

			if (!TextureToSample.IsValid())
			{
				return;
			}

			if (FTextureResource* TextureResource = TextureToSample->GetResource())
			{
				const FIntVector Size = TextureResource->TextureRHI->GetSizeXYZ();
				TArray<FLinearColor> Data;
				FIntRect Rect(InPixelCoordinates.X, InPixelCoordinates.Y, InPixelCoordinates.X + 1, InPixelCoordinates.Y + 1);
				RHICmdList.ReadSurfaceData(TextureResource->TextureRHI, Rect, Data, FReadSurfaceDataFlags());

				if (!Data.IsEmpty())
				{
					{
						FScopeLock Lock(&This->SampleCS);

						This->PixelColorSample = {
							InPixelCoordinates,
							InTime,
							Data[0]
						};

						This->bDirty = false;
					}
					return;
				}
			}

			{
				FScopeLock Lock(&This->SampleCS);
				This->PixelColorSample.Reset();
			}
		}
	);
}

void FTextureSampleCache::SetPixelColor_RenderTarget(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime)
{
	UTexture* Texture = TextureWeak.Get();

	if (!Texture)
	{
		return;
	}

	if (!RenderTarget)
	{
		RenderTarget = FMediaViewerUtils::CreateRenderTarget(
			FIntPoint(
				FMath::RoundToInt(Texture->GetSurfaceWidth()),
				FMath::RoundToInt(Texture->GetSurfaceHeight())
			),
			/* Transparent */ true
		);
	}

	bool bNeedsUpdate = false;

	{
		FScopeLock Lock(&SampleCS);
		bNeedsUpdate = !PixelColorSample.IsSet() || InTime != PixelColorSample->Time;
	}

	if (!bNeedsUpdate)
	{
		SetPixelColor_RHI(InPixelCoordinates, InTime, RenderTarget);
		return;
	}

	FRenderComplete OnComplete = FRenderComplete::CreateSPLambda(
		this,
		[this, InPixelCoordinates, InTime](bool bSuccess)
		{
			if (bSuccess)
			{
				SetPixelColor_RHI(InPixelCoordinates, InTime, RenderTarget);
			}
		}
	);

	ENQUEUE_RENDER_COMMAND(CopyViewportRenderTarget)(
		[Source = Texture, Target = RenderTarget, OnComplete](FRHICommandListImmediate& InRHICmdList)
		{
			FTextureResource* SourceResource = Source->GetResource();

			if (!SourceResource)
			{
				OnComplete.ExecuteIfBound(false);
				return;
			}

			constexpr uint32 MinDimension = 3;

			if (SourceResource->GetSizeX() < MinDimension || SourceResource->GetSizeY() < MinDimension)
			{
				OnComplete.ExecuteIfBound(false);
				return;
			}

			FTextureResource* TargetResource = Target->GetResource();

			if (!TargetResource)
			{
				OnComplete.ExecuteIfBound(false);
				return;
			}

			if (TargetResource->GetSizeX() < MinDimension || TargetResource->GetSizeY() < MinDimension)
			{
				OnComplete.ExecuteIfBound(false);
				return;
			}

			FRDGBuilder GraphBuilder(InRHICmdList);
			
			FRDGTexture* InputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceResource->TextureRHI, TEXT("SourceTexture")));
			FRDGTexture* OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(TargetResource->TextureRHI, TEXT("TargetTexture")));
			const FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			AddDrawTexturePass(GraphBuilder, GlobalShaderMap, InputTexture, OutputTexture, FRDGDrawTextureInfo());

			GraphBuilder.Execute();

			OnComplete.ExecuteIfBound(true);
		}
	);
}

} // UE::MediaViewer::Private
