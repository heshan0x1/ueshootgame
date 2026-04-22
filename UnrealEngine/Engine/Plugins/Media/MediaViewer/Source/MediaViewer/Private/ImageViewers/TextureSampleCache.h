// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/GCObject.h"

#include "HAL/CriticalSection.h"
#include "Math/Color.h"
#include "Math/IntPoint.h"
#include "Misc/NotNull.h"
#include "Misc/Optional.h"
#include "Misc/Timespan.h"
#include "UObject/WeakObjectPtr.h"

class UTexture;
class UTextureRenderTarget2D;
enum EPixelFormat : uint8;

namespace UE::MediaViewer::Private
{

class FTextureSampleCache : public TSharedFromThis<FTextureSampleCache>, public FGCObject
{
public:
	FTextureSampleCache();
	FTextureSampleCache(TNotNull<UTexture*> InTexture, EPixelFormat InPixelFormat);

	bool IsValid() const;

	const FLinearColor* GetPixelColor(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime = {});

	void MarkDirty();

	void Invalidate();

	FTextureSampleCache& operator=(const FTextureSampleCache& InOther);

	//~ Begin FGCObject
	virtual FString GetReferencerName() const override;
	virtual void AddReferencedObjects(FReferenceCollector& InCollector) override;
	//~ End FGCObject

protected:
	struct FPixelColorSample
	{
		FIntPoint Coordinates;
		TOptional<FTimespan> Time;
		FLinearColor Color;
	};

	TWeakObjectPtr<UTexture> TextureWeak;
	bool bDirty = true;
	TOptional<FPixelColorSample> PixelColorSample;
	mutable FCriticalSection SampleCS;
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	bool NeedsUpdate(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime) const;

	void SetPixelColor_RHI(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime, UTexture* InTexture);

	void SetPixelColor_RenderTarget(const FIntPoint& InPixelCoordinates, const TOptional<FTimespan>& InTime);
};

} // UE::MediaViewer::Private
