// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Widgets/Docking/SDockTab.h"

class FUICommandInfo;
class SBorder;
class SWidget;

class FEditorDiagnosticsCommands : public TCommands<FEditorDiagnosticsCommands>
{
public:
	FEditorDiagnosticsCommands();

	// TCommand<> interface
	virtual void RegisterCommands() override;
	// End of TCommand<> interface

public:
	TSharedPtr<FUICommandInfo> SetShowActivityMonitor;
	TSharedPtr<FUICommandInfo> SetShowStalls;
};

class SEditorDiagnosticsTab : public SDockTab
{
public:
	void Construct(const FArguments& InArgs);

protected:
	TSharedRef<SWidget> CreateToolBar();

	TSharedRef<SWidget> CreateActivityMonitorPanel();
	TSharedRef<SWidget> CreateStallLogPanel();

	void SetShowActivityMonitor();
	void SetShowStalls();

private:
	TWeakPtr<SBorder> ContentBorder;
	TWeakPtr<FUICommandInfo> SelectedCommand;
};
