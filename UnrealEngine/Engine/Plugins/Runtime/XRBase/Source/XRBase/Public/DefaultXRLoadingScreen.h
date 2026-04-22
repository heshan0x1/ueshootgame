// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "HAL/PlatformCrt.h"
#include "IStereoLayers.h"
#include "IXRLoadingScreen.h"
#include "XRLoadingScreenBase.h"
#include "UObject/GCObject.h"

#define UE_API XRBASE_API

class IStereoLayers;

/**
 * Default Loading Screen implementation based on the IStereoLayer interface.
 * It requires an XR tracking system with stereo rendering and stereo layers support.
 */

struct FSplashData {
	IXRLoadingScreen::FSplashDesc	Desc;
	uint32		LayerId;

	FSplashData(const IXRLoadingScreen::FSplashDesc& InDesc);
};

class FDefaultXRLoadingScreen : public TXRLoadingScreenBase<FSplashData>, public FGCObject
{
public:
	UE_API FDefaultXRLoadingScreen(class IXRTrackingSystem* InTrackingSystem);

	/* IXRLoadingScreen interface */
	UE_API virtual void ShowLoadingScreen() override;
	UE_API virtual void HideLoadingScreen() override;
	UE_API virtual bool IsPlayingLoadingMovie() const override;

	/* FGCObject interface */
	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDefaultXRLoadingScreen"); }

private:

	UE_API IStereoLayers* GetStereoLayers() const;

protected:
	UE_API virtual void DoShowSplash(FSplashData& Splash) override;
	UE_API virtual void DoHideSplash(FSplashData& Splash) override;
	virtual void DoAddSplash(FSplashData& Splash) override {}
	UE_API virtual void DoDeleteSplash(FSplashData& Splash) override;
	UE_API virtual void ApplyDeltaRotation(const FSplashData& Splash) override;

};

#undef UE_API
