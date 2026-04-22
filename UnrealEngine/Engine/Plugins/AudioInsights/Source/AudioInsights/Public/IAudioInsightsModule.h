// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsModuleInterface.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"
#include "Widgets/Docking/SDockTab.h"

#define UE_API AUDIOINSIGHTS_API

class IAudioInsightsTraceModule;

namespace UE::Audio::Insights
{
	class IDashboardViewFactory;
	class FAudioInsightsCacheManager;
	class FTraceProviderBase;
} // namespace UE::Audio::Insights

class IAudioInsightsModule : public IAudioInsightsModuleInterface
{
public:
	virtual void RegisterDashboardViewFactory(TSharedRef<UE::Audio::Insights::IDashboardViewFactory> InDashboardFactory) = 0;
	virtual void UnregisterDashboardViewFactory(FName InName) = 0;

	virtual ::Audio::FDeviceId GetDeviceId() const = 0;

	UE_API virtual IAudioInsightsTraceModule& GetTraceModule() override;

#if WITH_EDITOR
	UE_API virtual UE::Audio::Insights::FAudioInsightsCacheManager& GetCacheManager() override;
#endif

	virtual TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args) = 0;

	static UE_API IAudioInsightsModule& GetChecked();

#ifdef WITH_EDITOR
	static UE_API IAudioInsightsModule& GetEditorChecked();
#endif

};

#undef UE_API
