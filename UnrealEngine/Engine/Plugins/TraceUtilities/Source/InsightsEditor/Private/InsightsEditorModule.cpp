// Copyright Epic Games, Inc. All Rights Reserved.

#include "InsightsEditorModule.h"

#include "Framework/Docking/LayoutExtender.h"
#include "Modules/ModuleManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

// Insights
#include "Insights/IUnrealInsightsModule.h"

DEFINE_LOG_CATEGORY(LogInsightsEditor)

#define LOCTEXT_NAMESPACE "FInsightsEditorModule"

static bool GEnableInsightsUEFNMode = false;
static FAutoConsoleVariableRef CVAREnableTimingInsights(
	TEXT("Insights.EnableUEFNMode"),
	GEnableInsightsUEFNMode,
	TEXT("Enables the Insights UEFN features."),
	ECVF_Default);

namespace UE::InsightsEditor
{

void FInsightsEditorModule::StartupModule()
{
	LLM_SCOPE_BYNAME(TEXT("Insights/InsightsEditor"));

	FInsightsMajorTabConfig TimingProfilerConfig;

	TimingProfilerConfig.Layout = FTabManager::NewLayout("EditorTimingInsightsLayout_v1.0")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FTimingProfilerTabs::ToolbarID, ETabState::ClosedTab)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.7f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.1f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::FramesTrackID, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.9f)
						->SetHideTabWell(true)
						->AddTab(FTimingProfilerTabs::TimingViewID, ETabState::OpenedTab)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab("DocumentTab", ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::TimersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::StatsCountersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::CallersID, ETabState::ClosedTab)
					->AddTab(FTimingProfilerTabs::CalleesID, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(FTimingProfilerTabs::LogViewID, ETabState::ClosedTab)
			)
		);

	TimingProfilerConfig.WorkspaceGroup = WorkspaceMenu::GetMenuStructure().GetDeveloperToolsProfilingCategory();

	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");

	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::TimingProfilerTabId, TimingProfilerConfig);
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::SessionInfoTabId, FInsightsMajorTabConfig::Unavailable());
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::LoadingProfilerTabId, FInsightsMajorTabConfig::Unavailable());
	UnrealInsightsModule.RegisterMajorTabConfig(FInsightsManagerTabs::NetworkingProfilerTabId, FInsightsMajorTabConfig::Unavailable());

	UnrealInsightsModule.SetUnrealInsightsLayoutIni(GEditorLayoutIni);

	UnrealInsightsModule.OnMajorTabCreated().AddLambda([this](const FName& InMajorTabId, TSharedPtr<FTabManager> InTabManager)
		{
			if (bStartAnalysisOnInsightsWindowCreated && InMajorTabId != FInsightsManagerTabs::MemoryProfilerTabId)
			{
				StartTraceAnalysis();
			}
		});

	// Create store and start analysis session - should only be done after engine has init and all plugins are loaded
	FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([this]
		{
			IConsoleVariable* CVarEnableUEFNMode = IConsoleManager::Get().FindConsoleVariable(TEXT("Insights.EnableUEFNMode"));
			if (CVarEnableUEFNMode && CVarEnableUEFNMode->GetBool() == false)
			{
				LLM_SCOPE_BYNAME(TEXT("Insights/InsightsEditor"));
				IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
				if (!UnrealInsightsModule.GetStoreClient())
				{
					UE_LOG(LogCore, Display, TEXT("InsightsEditor module auto-connecting to local trace server..."));
					UnrealInsightsModule.ConnectToStore(TEXT("127.0.0.1"));
					UnrealInsightsModule.CreateSessionViewer(false);
				}
			}
		});
}

void FInsightsEditorModule::ShutdownModule()
{
	LLM_SCOPE_BYNAME(TEXT("Insights/InsightsEditor"));
}

void FInsightsEditorModule::StartTraceAnalysis()
{
	LLM_SCOPE_BYNAME(TEXT("Insights/InsightsEditor"));

	IUnrealInsightsModule& UnrealInsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
	if (!UnrealInsightsModule.GetAnalysisSession().IsValid())
	{
		UnrealInsightsModule.StartAnalysisForLastLiveSession();
	}
}
} // namespace UE::InsightsEditor

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(UE::InsightsEditor::FInsightsEditorModule, InsightsEditor)