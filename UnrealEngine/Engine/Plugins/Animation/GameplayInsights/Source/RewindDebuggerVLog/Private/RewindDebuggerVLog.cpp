// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerVLog.h"

#include "FindInBlueprintManager.h"
#include "IRewindDebugger.h"
#include "IVisualLoggerProvider.h"
#include "LogVisualizerSettings.h"
#include "ObjectTrace.h"
#include "RewindDebuggerVLogSettings.h"
#include "ToolMenus.h"
#include "VisualLogEntryRenderer.h"
#include "Debug/DebugDrawService.h"
#include "Editor/EditorEngine.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TraceServices/Model/Frames.h"
#include "VisualLogger/VisualLoggerTraceDevice.h"

#define LOCTEXT_NAMESPACE "RewindDebuggerVLog"

TAutoConsoleVariable<int32> CVarRewindDebuggerVLogUseActor(TEXT("a.RewindDebugger.VisualLogs.UseActor"), 0, TEXT("Use actor based debug renderer for visual logs"));

void FRewindDebuggerVLog::OnShowDebugInfo(UCanvas* Canvas, APlayerController* Player)
{
	ScreenTextY = 60;
	if (IRewindDebugger* RewindDebugger = IRewindDebugger::Instance())
	{
		if (RewindDebugger->IsPIESimulating())
		{
			// make sure this is the primary view, when we are playing in PIE, so we don't clear ImmediateRenderQueue when this has been called on some other editor view.
			if (Canvas->SceneView->ViewActor)
			{
				for (FVisualLogEntry& Entry : ImmediateRenderQueue)
				{
					RenderLogEntry(Entry, Canvas);
				}

				ImmediateRenderQueue.SetNum(0);
			}
		}
		else
		{
			ObjectsVisited.Empty();
			if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
			{
				TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
				double CurrentTraceTime = RewindDebugger->CurrentTraceTime();

				const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
				TraceServices::FFrame CurrentFrame;

				if (FrameProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, CurrentTraceTime, CurrentFrame))
				{
					if (const IVisualLoggerProvider* VisualLoggerProvider = Session->ReadProvider<IVisualLoggerProvider>("VisualLoggerProvider"))
					{
						AddLogEntries(RewindDebugger->GetDebuggedObjects(), CurrentFrame.StartTime, CurrentFrame.EndTime, VisualLoggerProvider, Canvas);
					}
				}
			}
		}
	}
}

FRewindDebuggerVLog::~FRewindDebuggerVLog()
{
	UDebugDrawService::Unregister(DelegateHandle);
}

void FRewindDebuggerVLog::Initialize()
{
	UToolMenu* Menu = UToolMenus::Get()->FindMenu("RewindDebugger.ToolBar");

	FToolMenuSection& NewSection = Menu->AddSection("Visual Logger", LOCTEXT("Visual Logger", "Visual Logger"));

	NewSection.AddSeparator("VisualLogger");

	NewSection.AddEntry(FToolMenuEntry::InitComboButton(
		"VLog Categories",
		FUIAction(),
		FNewToolMenuDelegate::CreateRaw(this, &FRewindDebuggerVLog::MakeCategoriesMenu),
		LOCTEXT("VLog Categories", "VLog Categories")
	));

	NewSection.AddEntry(FToolMenuEntry::InitComboButton(
		"VLog Level",
		FUIAction(),
		FNewToolMenuDelegate::CreateRaw(this, &FRewindDebuggerVLog::MakeLogLevelMenu),
		LOCTEXT("VLog Level", "VLog Level")));

	FVisualLoggerTraceDevice& TraceDevice = FVisualLoggerTraceDevice::Get();
	TraceDevice.ImmediateRenderDelegate.BindRaw(this, &FRewindDebuggerVLog::ImmediateRender);

	DelegateHandle = UDebugDrawService::Register(TEXT("VirtualTextureResidency")/*TEXT("VisLog")*/, FDebugDrawDelegate::CreateRaw(this, &FRewindDebuggerVLog::OnShowDebugInfo));

	MonospaceFont = TStrongObjectPtr<UFont>(NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient));
	MonospaceFont->FontCacheType = EFontCacheType::Runtime;
	MonospaceFont->RuntimeFontSource = ERuntimeFontSource::CoreStyleDefault;
	MonospaceFont->LegacyFontName = FName("Mono");
	MonospaceFont->LegacyFontSize = 9;
}

bool MatchCategoryFilters(const FName& CategoryName, ELogVerbosity::Type Verbosity)
{
	URewindDebuggerVLogSettings& Settings = URewindDebuggerVLogSettings::Get();
	return Settings.DisplayCategories.Contains(CategoryName) && Verbosity <= Settings.DisplayVerbosity;
}

void FRewindDebuggerVLog::RenderLogEntry(const FVisualLogEntry& Entry, UCanvas* Canvas)
{
	if (CVarRewindDebuggerVLogUseActor.GetValueOnAnyThread())
	{
		// old actor based code path
		if (AVLogRenderingActor* RenderingActor = GetRenderingActor())
		{
			RenderingActor->AddLogEntry(Entry);
		}
	}
	else
	{
		UWorld* World = IRewindDebugger::Instance()->GetWorldToVisualize();
		FVisualLogEntryRenderer::RenderLogEntry(World, Entry, &MatchCategoryFilters, Canvas, GEngine->GetMediumFont(), MonospaceFont.Get(), ScreenTextY);

	}
}

void FRewindDebuggerVLog::ImmediateRender(const UObject* Object, const FVisualLogEntry& Entry)
{
#if OBJECT_TRACE_ENABLED
	uint64 ObjectId = FObjectTrace::GetObjectId(Object);
	if (DebuggedObjectIds.Contains(ObjectId))
	{
		ImmediateRenderQueue.Add(Entry);
	}
#endif
}

bool FRewindDebuggerVLog::IsCategoryActive(const FName& Category)
{
	URewindDebuggerVLogSettings& Settings = URewindDebuggerVLogSettings::Get();
	return Settings.DisplayCategories.Contains(Category);
}

void FRewindDebuggerVLog::ToggleCategory(const FName& Category)
{
	URewindDebuggerVLogSettings::Get().ToggleCategory(Category);

}

ELogVerbosity::Type FRewindDebuggerVLog::GetMinLogVerbosity()
{
	return static_cast<ELogVerbosity::Type>(URewindDebuggerVLogSettings::Get().DisplayVerbosity);
}

void FRewindDebuggerVLog::SetMinLogVerbosity(ELogVerbosity::Type Value)
{
	URewindDebuggerVLogSettings::Get().SetMinVerbosity(Value);

}

void FRewindDebuggerVLog::MakeLogLevelMenu(UToolMenu* Menu) const
{
	FToolMenuSection& Section = Menu->AddSection("Levels");

	for (ELogVerbosity::Type LogVerbosityLevel = ELogVerbosity::All; LogVerbosityLevel > 0; LogVerbosityLevel = static_cast<ELogVerbosity::Type>(LogVerbosityLevel - 1))
	{
		FString Name = ToString(LogVerbosityLevel);
		FText Label(FText::FromString(Name));

		Section.AddMenuEntry(FName(Name),
			Label,
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([LogVerbosityLevel, this]() { SetMinLogVerbosity(LogVerbosityLevel); }),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([LogVerbosityLevel, this]() { return GetMinLogVerbosity() == LogVerbosityLevel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			),
			EUserInterfaceActionType::Check
		);
	}
}

void FRewindDebuggerVLog::MakeCategoriesMenu(UToolMenu* Menu) const
{
	FToolMenuSection& Section = Menu->AddSection("Categories");
	IRewindDebugger* RewindDebugger = IRewindDebugger::Instance();
	if (const TraceServices::IAnalysisSession* Session = RewindDebugger->GetAnalysisSession())
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session);
		if (const IVisualLoggerProvider* VisualLoggerProvider = Session->ReadProvider<IVisualLoggerProvider>("VisualLoggerProvider"))
		{
			VisualLoggerProvider->EnumerateCategories([&Section, this](const FName& Category)
				{
					Section.AddMenuEntry(Category,
						FText::FromName(Category),
						FText(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([Category, this]() { ToggleCategory(Category); }),
							FCanExecuteAction(),
							FGetActionCheckState::CreateLambda([Category, this]() { return IsCategoryActive(Category) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						),
						EUserInterfaceActionType::Check
					);
				});
		}
	}
}

void FRewindDebuggerVLog::AddLogEntries(const TArray<TSharedPtr<FDebugObjectInfo>>& Components, float StartTime, float EndTime, const IVisualLoggerProvider* VisualLoggerProvider, UCanvas* Canvas)
{
	for (const TSharedPtr<FDebugObjectInfo>& ComponentInfo : Components)
	{
		const uint64 ObjectId = ComponentInfo->GetUObjectId();
		if (!ObjectsVisited.Contains(ObjectId))
		{
			ObjectsVisited.Add(ObjectId);
			VisualLoggerProvider->ReadVisualLogEntryTimeline(ObjectId, [this, StartTime, EndTime, Canvas](const IVisualLoggerProvider::VisualLogEntryTimeline& TimelineData)
				{
					TimelineData.EnumerateEvents(StartTime, EndTime, [this, StartTime, EndTime, Canvas](double InStartTime, double InEndTime, uint32 InDepth, const FVisualLogEntry& LogEntry)
						{
							if (InStartTime >= StartTime && InStartTime <= EndTime)
							{
								RenderLogEntry(LogEntry, Canvas);
							}
							return TraceServices::EEventEnumerate::Continue;
						});
				});
		}

		AddLogEntries(ComponentInfo->Children, StartTime, EndTime, VisualLoggerProvider, Canvas);
	}
}

AVLogRenderingActor* FRewindDebuggerVLog::GetRenderingActor()
{
	if (!VLogActor.IsValid())
	{
		UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
		if (GIsEditor && EditorEngine && EditorEngine->PlayWorld)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.ObjectFlags |= RF_Transient;
			VLogActor = EditorEngine->PlayWorld->SpawnActor<AVLogRenderingActor>(SpawnParameters);
		}
	}
	return VLogActor.Get();
}

void FRewindDebuggerVLog::Update(float DeltaTime, IRewindDebugger* RewindDebugger)
{
}

void FRewindDebuggerVLog::OnTrackListChanged(IRewindDebugger* RewindDebugger)
{
	const TArray<TSharedPtr<FDebugObjectInfo>>& DebuggedObjects = RewindDebugger->GetDebuggedObjects();
	DebuggedObjectIds.Reset(DebuggedObjects.Num());
	for (const TSharedPtr<FDebugObjectInfo>& Object : DebuggedObjects)
	{
		if (Object->Id.IsSet())
		{
			DebuggedObjectIds.Push(Object->GetUObjectId());
		}
	}
}

#undef LOCTEXT_NAMESPACE
