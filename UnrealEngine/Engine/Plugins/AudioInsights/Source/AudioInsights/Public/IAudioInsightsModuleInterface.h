// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "Cache/AudioInsightsCacheManager.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

class IAudioInsightsTraceModule;

namespace UE::Audio::Insights
{
	class IDashboardViewFactory;
	class FTraceProviderBase;
} // namespace UE::Audio::Insights

class IAudioInsightsModuleInterface : public IModuleInterface
{
public:
	virtual void RegisterDashboardViewFactory(TSharedRef<UE::Audio::Insights::IDashboardViewFactory> InDashboardFactory) = 0;
	virtual void UnregisterDashboardViewFactory(FName InName) = 0;

	virtual ::Audio::FDeviceId GetDeviceId() const = 0;

	virtual IAudioInsightsTraceModule& GetTraceModule() = 0;

#if WITH_EDITOR
	virtual UE::Audio::Insights::FAudioInsightsCacheManager& GetCacheManager() = 0;
#endif // WITH_EDITOR
};
