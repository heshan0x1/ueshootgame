// Copyright Epic Games, Inc. All Rights Reserved.

#include "AvaTickerActorTool.h"
#include "AvaEditorCommands.h"
#include "Builders/AvaInteractiveToolsToolBuilder.h"
#include "Framework/Ticker/AvaTickerActor.h"

UAvaTickerActorTool::UAvaTickerActorTool()
{
	ActorClass = AAvaTickerActor::StaticClass();
}

void UAvaTickerActorTool::OnRegisterTool(IAvalancheInteractiveToolsModule* InAITModule)
{
	Super::OnRegisterTool(InAITModule);

	FAvaInteractiveToolsToolParameters ToolParameters = CreateDefaultToolParameters();
	ToolParameters.UICommand = FAvaEditorCommands::Get().TickerTool;

	InAITModule->RegisterTool(IAvalancheInteractiveToolsModule::CategoryNameActor, MoveTemp(ToolParameters));
}
