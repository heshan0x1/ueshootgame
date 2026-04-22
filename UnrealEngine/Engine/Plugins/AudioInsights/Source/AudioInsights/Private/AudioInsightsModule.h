// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "AudioInsightsDashboardFactory.h"
#include "AudioInsightsTimingViewExtender.h"
#include "AudioInsightsTraceModule.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsModule.h"
#include "Insights/IUnrealInsightsModule.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Views/DashboardViewFactory.h"
#include "Widgets/Docking/SDockTab.h"


namespace UE::Audio::Insights
{
#if !WITH_EDITOR
	class FAudioInsightsComponent;
#endif // !WITH_EDITOR

	class FAudioInsightsModule final : public IAudioInsightsModule
	{
	public:
		FAudioInsightsModule() = default;
		virtual ~FAudioInsightsModule() = default;

		FAudioInsightsModule(const FAudioInsightsModule&) = delete;
		FAudioInsightsModule& operator=(const FAudioInsightsModule&) = delete;
		FAudioInsightsModule(FAudioInsightsModule&&) = delete;
		FAudioInsightsModule& operator=(FAudioInsightsModule&&) = delete;

		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		virtual void RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory) override;
		virtual void UnregisterDashboardViewFactory(FName InName) override;
		virtual ::Audio::FDeviceId GetDeviceId() const override;

		static FAudioInsightsModule& GetChecked();
		static FAudioInsightsModule* GetModulePtr();
		virtual IAudioInsightsTraceModule& GetTraceModule() override;

#if WITH_EDITOR
		virtual class FAudioInsightsCacheManager& GetCacheManager() override;
#endif // WITH_EDITOR

#if !WITH_EDITOR
		TSharedPtr<FAudioInsightsComponent> GetAudioInsightsComponent() { return AudioInsightsComponent; };
#endif // !WITH_EDITOR

		FAudioInsightsTimingViewExtender& GetTimingViewExtender() { return AudioInsightsTimingViewExtender; };
		const FAudioInsightsTimingViewExtender& GetTimingViewExtender() const { return AudioInsightsTimingViewExtender; };

		TSharedRef<FDashboardFactory> GetDashboardFactory();
		const TSharedRef<FDashboardFactory> GetDashboardFactory() const;

		virtual TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args) override;

	private:
		TSharedPtr<FDashboardFactory> DashboardFactory;
		TUniquePtr<FTraceModule> TraceModule;
		TUniquePtr<FRewindDebuggerAudioInsightsRuntime> RewindDebuggerExtension;

#if WITH_EDITOR
		TUniquePtr<class FAudioInsightsCacheManager> CacheManager;
#endif // WITH_EDITOR

#if !WITH_EDITOR
		TSharedPtr<FAudioInsightsComponent> AudioInsightsComponent;
#endif // !WITH_EDITOR

		FAudioInsightsTimingViewExtender AudioInsightsTimingViewExtender;
	};
} // namespace UE::Audio::Insights
