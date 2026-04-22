// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "AudioInsightsEditorDashboardFactory.h"
#include "AudioInsightsTraceModule.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsEditorModule.h"
#include "Templates/SharedPointer.h"
#include "Views/DashboardViewFactory.h"
#include "Views/VirtualLoopsDebugDraw.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::Audio::Insights
{
	class FAudioInsightsEditorModule final : public IAudioInsightsEditorModule
	{
	public:
		FAudioInsightsEditorModule() = default;
		virtual ~FAudioInsightsEditorModule() = default;

		FAudioInsightsEditorModule(const FAudioInsightsEditorModule&) = delete;
		FAudioInsightsEditorModule& operator=(const FAudioInsightsEditorModule&) = delete;
		FAudioInsightsEditorModule(FAudioInsightsEditorModule&&) = delete;
		FAudioInsightsEditorModule& operator=(FAudioInsightsEditorModule&&) = delete;

		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		virtual void RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory) override;
		virtual void UnregisterDashboardViewFactory(FName InName) override;
		virtual ::Audio::FDeviceId GetDeviceId() const override;

		TSharedPtr<FEditorDashboardFactory> GetDashboardFactory();
		const TSharedPtr<FEditorDashboardFactory> GetDashboardFactory() const;

		static bool IsModuleLoaded();
		static FAudioInsightsEditorModule& GetChecked();
		virtual IAudioInsightsTraceModule& GetTraceModule() override;

#if WITH_EDITOR
		virtual class FAudioInsightsCacheManager& GetCacheManager() override;
#endif // WITH_EDITOR

	private:
		void RegisterMenus();
		TSharedRef<SDockTab> CreateDashboardTabWidget(const FSpawnTabArgs& Args);
		
		inline static const FName AudioInsightsEditorModuleName = "AudioInsightsEditor";

		TSharedPtr<FEditorDashboardFactory> DashboardFactory;
		
		FVirtualLoopsDebugDraw VirtualLoopsDebugDraw;
	};
} // namespace UE::Audio::Insights
