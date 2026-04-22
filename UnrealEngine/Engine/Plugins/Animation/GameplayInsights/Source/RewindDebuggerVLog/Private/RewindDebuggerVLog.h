// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IRewindDebugger.h"
#include "IRewindDebuggerExtension.h"
#include "VLogRenderingActor.h"
#include "ToolMenu.h"
// Needed for TStrongObjectPtr which requires complete type
#include "Engine/Font.h"

// Rewind debugger extension for Visual Logger support

class FRewindDebuggerVLog : public IRewindDebuggerExtension
{
public:
	void OnShowDebugInfo(UCanvas* Canvas, APlayerController* Player);
	virtual ~FRewindDebuggerVLog();

	virtual FString GetName()
	{
		return TEXT("FRewindDebuggerVLog");
	}

	void Initialize();
	void MakeCategoriesMenu(UToolMenu* Menu) const;
	void MakeLogLevelMenu(UToolMenu* Menu) const;
	static void ToggleCategory(const FName& Category);
	static bool IsCategoryActive(const FName& Category);

	static ELogVerbosity::Type GetMinLogVerbosity();
	static void SetMinLogVerbosity(ELogVerbosity::Type Value);

protected:
	virtual void Update(float DeltaTime, IRewindDebugger* RewindDebugger) override;
	virtual void OnTrackListChanged(IRewindDebugger* RewindDebugger) override;

private:
	void AddLogEntries(const TArray<TSharedPtr<FDebugObjectInfo>>& Components, float StartTime, float EndTime, const class IVisualLoggerProvider* Provider, UCanvas* Canvas);
	void ImmediateRender(const UObject* Object, const FVisualLogEntry& Entry);
	void RenderLogEntry(const FVisualLogEntry& Entry, UCanvas* Canvas);

	AVLogRenderingActor* GetRenderingActor();

	TWeakObjectPtr<AVLogRenderingActor> VLogActor;

	TSet<uint64> ObjectsVisited;
	int32 ScreenTextY = 0;

	FDelegateHandle DelegateHandle;
	TStrongObjectPtr<UFont> MonospaceFont;

	TArray<uint64> DebuggedObjectIds;
	TArray<FVisualLogEntry> ImmediateRenderQueue;
};
