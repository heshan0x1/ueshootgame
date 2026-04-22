// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioInsightsModule.h"

#include "AudioInsightsDashboardAssetCommands.h"
#include "AudioInsightsDashboardFactory.h"
#include "AudioInsightsLog.h"
#include "AudioInsightsStyle.h"
#include "AudioInsightsTraceModule.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "TraceServices/ModuleService.h"
#include "UObject/NameTypes.h"

#if !WITH_EDITOR
#include "AudioInsightsComponent.h"
#include "Views/AudioBusDashboardViewFactory.h"
#include "Views/AudioEventLogDashboardViewFactory.h"
#include "Views/AudioMetersPanelDashboardViewFactory.h"
#include "Views/OutputMeterDashboardViewFactory.h"
#include "Views/SoundDashboardViewFactory.h"
#include "Views/SoundPlotsDashboardViewFactory.h"
#include "Views/SubmixDashboardViewFactory.h"
#include "Views/VirtualLoopDashboardViewFactory.h"
#endif // !WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioInsights"
DEFINE_LOG_CATEGORY(LogAudioInsights);


namespace UE::Audio::Insights
{
	void FAudioInsightsModule::StartupModule()
	{
		// Don't run providers in any commandlet to avoid additional, unnecessary overhead as audio insights is dormant.
		if (!IsRunningCommandlet())
		{
			TraceModule = MakeUnique<FTraceModule>();
			RewindDebuggerExtension = MakeUnique<FRewindDebuggerAudioInsightsRuntime>();

			IModularFeatures::Get().RegisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
			IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, RewindDebuggerExtension.Get());

			DashboardFactory = MakeShared<FDashboardFactory>();
			
			FDashboardAssetCommands::Register();

#if WITH_EDITOR
			CacheManager = MakeUnique<FAudioInsightsCacheManager>();
#endif // WITH_EDITOR

#if !WITH_EDITOR
			IModularFeatures::Get().RegisterModularFeature(UE::Insights::Timing::TimingViewExtenderFeatureName, &AudioInsightsTimingViewExtender);

			AudioInsightsComponent = FAudioInsightsComponent::CreateInstance();

			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			UnrealInsightsModule.RegisterComponent(AudioInsightsComponent);

			TSharedRef<FSoundDashboardViewFactory> SoundsDashboard = MakeShared<FSoundDashboardViewFactory>();
			TSharedRef<FSoundPlotsDashboardViewFactory> PlotsDashboard = MakeShared<FSoundPlotsDashboardViewFactory>();
			TSharedRef<FSubmixDashboardViewFactory> SubmixDashboard = MakeShared<FSubmixDashboardViewFactory>();

			DashboardFactory->RegisterViewFactory(MakeShared<FAudioEventLogDashboardViewFactory>());
			DashboardFactory->RegisterViewFactory(SoundsDashboard);
			DashboardFactory->RegisterViewFactory(MakeShared<FVirtualLoopDashboardViewFactory>());
			DashboardFactory->RegisterViewFactory(PlotsDashboard);
			DashboardFactory->RegisterViewFactory(SubmixDashboard);
			DashboardFactory->RegisterViewFactory(MakeShared<FAudioBusDashboardViewFactory>());
			DashboardFactory->RegisterViewFactory(MakeShared<FAudioMetersPanelDashboardViewFactory>());
			DashboardFactory->RegisterViewFactory(MakeShared<FOutputMeterDashboardViewFactory>(SubmixDashboard));

			PlotsDashboard->InitPlots(SoundsDashboard);
#endif // !WITH_EDITOR

			FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/AudioInsights"));
				IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
				if (!UnrealInsightsModule.GetStoreClient())
				{
					UE_LOG(LogCore, Display, TEXT("AudioInsights module auto-connecting to local trace server..."));
					UnrealInsightsModule.ConnectToStore(TEXT("127.0.0.1"));
					UnrealInsightsModule.CreateSessionViewer(false);
				}
			});
		}
	}

	void FAudioInsightsModule::ShutdownModule()
	{
		if (!IsRunningCommandlet())
		{
#if !WITH_EDITOR
			IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
			UnrealInsightsModule.UnregisterComponent(AudioInsightsComponent);

			AudioInsightsComponent.Reset();

			IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, &AudioInsightsTimingViewExtender);
#endif // !WITH_EDITOR

			FDashboardAssetCommands::Unregister();

			DashboardFactory.Reset();

			IModularFeatures::Get().UnregisterModularFeature(TraceServices::ModuleFeatureName, TraceModule.Get());
			TraceModule.Reset();

			IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, RewindDebuggerExtension.Get());
			RewindDebuggerExtension.Reset();

#if WITH_EDITOR
			CacheManager.Reset();
#endif // WITH_EDITOR
		}
	}

	void FAudioInsightsModule::RegisterDashboardViewFactory(TSharedRef<IDashboardViewFactory> InDashboardFactory)
	{
		DashboardFactory->RegisterViewFactory(InDashboardFactory);
	}

	void FAudioInsightsModule::UnregisterDashboardViewFactory(FName InName)
	{
		DashboardFactory->UnregisterViewFactory(InName);
	}

	::Audio::FDeviceId FAudioInsightsModule::GetDeviceId() const
	{
		return DashboardFactory->GetDeviceId();
	}

	FAudioInsightsModule& FAudioInsightsModule::GetChecked()
	{
		return static_cast<FAudioInsightsModule&>(FModuleManager::LoadModuleChecked<IAudioInsightsModule>("AudioInsights"));
	}

	FAudioInsightsModule* FAudioInsightsModule::GetModulePtr()
	{
		if (IAudioInsightsModule* AudioInsightsModulePtr = FModuleManager::LoadModulePtr<IAudioInsightsModule>("AudioInsights"))
		{
			return static_cast<FAudioInsightsModule*>(AudioInsightsModulePtr);
		}

		return nullptr;
	}

	TSharedRef<FDashboardFactory> FAudioInsightsModule::GetDashboardFactory()
	{
		return DashboardFactory->AsShared();
	}

	const TSharedRef<FDashboardFactory> FAudioInsightsModule::GetDashboardFactory() const
	{
		return DashboardFactory->AsShared();
	}

	IAudioInsightsTraceModule& FAudioInsightsModule::GetTraceModule()
	{
		return *TraceModule;
	}

#if WITH_EDITOR
	FAudioInsightsCacheManager& FAudioInsightsModule::GetCacheManager()
	{
		return *CacheManager;
	}
#endif // WITH_EDITOR

	TSharedRef<SDockTab> FAudioInsightsModule::CreateDashboardTabWidget(const FSpawnTabArgs& Args)
	{
		return DashboardFactory->MakeDockTabWidget(Args);
	}
} // namespace UE::Audio::Insights
#undef LOCTEXT_NAMESPACE // AudioInsights

IMPLEMENT_MODULE(UE::Audio::Insights::FAudioInsightsModule, AudioInsights)
